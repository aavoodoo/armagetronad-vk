package org.armagetronad.game;

import org.libsdl.app.SDLActivity;

import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ShortcutInfo;
import android.content.pm.ShortcutManager;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.graphics.drawable.Icon;
import android.hardware.Sensor;
import android.hardware.SensorEvent;
import android.hardware.SensorEventListener;
import android.hardware.SensorManager;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.util.DisplayMetrics;
import android.view.Gravity;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.InputMethodManager;
import android.widget.FrameLayout;

public class ArmagetronActivity extends SDLActivity {

    // -----------------------------------------------------------------------
    // Native methods (implemented in rTouchBridgeAndroid.cpp)
    // -----------------------------------------------------------------------
    private static native void    nativeInjectSdlKey(int scancode);
    private static native void    nativeSetGyroCameraInput(float yaw, float pitch);
    private static native void    nativeSetGlanceForward(boolean active);
    private static native void    nativeSwitchCameraView();
    private static native boolean nativeIsGameRunning();
    private static native String  nativeGetInstantChatString(int playerIndex, int chatIndex);
    private static native void    nativeSetTouchMode(int mode);
    private static native boolean nativeIsChatAvailable();
    private static native void    nativeTriggerAction(String actionName);
    private static native void    nativeTriggerPlayerAction(String actionName);
    private static native int     nativeGetNumViewports();
    private static native boolean nativeGetViewportInfo(int vpIdx, float[] outRect, int[] outRot);

    // SDL scancode constants (USB HID based, same as SDL_scancode.h)
    static final int SC_RETURN = 40;
    static final int SC_ESCAPE = 41;
    static final int SC_TAB    = 43;
    static final int SC_GRAVE  = 53;
    // F1-F8 for instant chat slots
    static final int SC_F1 = 58, SC_F2 = 59, SC_F3 = 60, SC_F4 = 61;
    static final int SC_F5 = 62, SC_F6 = 63, SC_F7 = 64, SC_F8 = 65;

    private View          mOverlay;
    private Handler       mPollHandler      = new Handler();
    private boolean       mNativeLibLoaded  = false;

    // Gyro / sensor state
    private SensorManager        mSensorManager;
    private Sensor               mGravitySensor;
    private AAGyroButton         mGyroBtn;
    private float                mGyroBaseX = Float.NaN;
    private float                mGyroBaseY = Float.NaN;
    // Android gravity sensor values are in m/s² (max ~9.81); normalize by dividing by 9.81
    private static final float GYRO_G             = 9.81f;
    private static final float GYRO_YAW_SENS      = 3.0f / GYRO_G;  // rad/s per m/s²
    private static final float GYRO_PITCH_SENS    = 1.5f / GYRO_G;

    private final SensorEventListener mGravityListener = new SensorEventListener() {
        @Override
        public void onSensorChanged(SensorEvent event) {
            float gx = event.values[0]; // lateral axis
            float gy = event.values[1]; // longitudinal axis
            if (Float.isNaN(mGyroBaseX)) {
                mGyroBaseX = gx;
                mGyroBaseY = gy;
                return;
            }
            // Same sign convention as iOS: top-right tilt → look right → negative yaw
            float yaw   = -(gx - mGyroBaseX) * GYRO_YAW_SENS;
            float pitch =  (gy - mGyroBaseY) * GYRO_PITCH_SENS;
            if (mNativeLibLoaded) {
                try { nativeSetGyroCameraInput(yaw, pitch); } catch (Throwable ignored) {}
            }
        }
        @Override public void onAccuracyChanged(Sensor sensor, int accuracy) {}
    };

    void onGyroToggled(boolean enabled) {
        if (mSensorManager == null || mGravitySensor == null) return;
        if (enabled) {
            mGyroBaseX = Float.NaN;
            mGyroBaseY = Float.NaN;
            mSensorManager.registerListener(mGravityListener, mGravitySensor,
                    SensorManager.SENSOR_DELAY_GAME);
        } else {
            mSensorManager.unregisterListener(mGravityListener);
            if (mNativeLibLoaded) try { nativeSetGyroCameraInput(0, 0); } catch (Throwable ignored) {}
        }
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL3", "armagetronad" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        mNativeLibLoaded = true;
        mSensorManager  = (SensorManager) getSystemService(Context.SENSOR_SERVICE);
        if (mSensorManager != null)
            mGravitySensor = mSensorManager.getDefaultSensor(Sensor.TYPE_GRAVITY);
        setupTouchOverlay();
        requestHomeScreenShortcut();
    }

    /** Asks the launcher to pin an icon to the home screen (Android 8+, once per install). */
    private void requestHomeScreenShortcut() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.O) return;
        SharedPreferences prefs = getPreferences(MODE_PRIVATE);
        if (prefs.getBoolean("shortcut_requested", false)) return;

        ShortcutManager sm = getSystemService(ShortcutManager.class);
        if (sm == null || !sm.isRequestPinShortcutSupported()) return;

        Intent launchIntent = new Intent(this, ArmagetronActivity.class);
        launchIntent.setAction(Intent.ACTION_MAIN);

        ShortcutInfo shortcut = new ShortcutInfo.Builder(this, "main")
                .setShortLabel(getString(R.string.app_name))
                .setLongLabel(getString(R.string.app_name))
                .setIcon(Icon.createWithResource(this, R.mipmap.ic_launcher))
                .setIntent(launchIntent)
                .build();

        sm.requestPinShortcut(shortcut, null);
        prefs.edit().putBoolean("shortcut_requested", true).apply();
    }

    @Override
    protected void onDestroy() {
        mPollHandler.removeCallbacksAndMessages(null);
        if (mSensorManager != null) mSensorManager.unregisterListener(mGravityListener);
        super.onDestroy();
    }

    // -----------------------------------------------------------------------
    // Overlay setup
    // -----------------------------------------------------------------------
    private void setupTouchOverlay() {
        // Pass-through container — intercepts nothing by itself
        FrameLayout overlay = new FrameLayout(this) {
            @Override public boolean onInterceptTouchEvent(MotionEvent e) { return false; }
            @Override public boolean onTouchEvent(MotionEvent e)          { return false; }
        };
        overlay.setClickable(false);
        overlay.setFocusable(false);

        float dp = getResources().getDisplayMetrics().density;
        int btnW    = Math.round(64 * dp);
        int btnH    = Math.round(44 * dp);
        int subBtnH = Math.round(28 * dp);
        int margin  = Math.round(4  * dp);

        AACornerButton left  = new AACornerButton(this, false);
        AACornerButton right = new AACornerButton(this, true);

        FrameLayout.LayoutParams lp, rp;

        lp = new FrameLayout.LayoutParams(btnW, btnH);
        lp.gravity = Gravity.TOP | Gravity.LEFT;
        lp.setMargins(margin, margin, 0, 0);
        overlay.addView(left, lp);

        rp = new FrameLayout.LayoutParams(btnW, btnH);
        rp.gravity = Gravity.TOP | Gravity.RIGHT;
        rp.setMargins(0, margin, margin, 0);
        overlay.addView(right, rp);

        // Gyro toggle button — below left main button
        mGyroBtn = new AAGyroButton(this);
        FrameLayout.LayoutParams gp = new FrameLayout.LayoutParams(btnW, subBtnH);
        gp.gravity = Gravity.TOP | Gravity.LEFT;
        gp.setMargins(margin, margin + btnH + margin, 0, 0);
        overlay.addView(mGyroBtn, gp);

        // Camera button — below right main button
        AACameraButton camBtn = new AACameraButton(this);
        FrameLayout.LayoutParams cp = new FrameLayout.LayoutParams(btnW, subBtnH);
        cp.gravity = Gravity.TOP | Gravity.RIGHT;
        cp.setMargins(0, margin + btnH + margin, margin, 0);
        overlay.addView(camBtn, cp);

        mOverlay = overlay;
        overlay.setVisibility(View.INVISIBLE);

        addContentView(overlay, new ViewGroup.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        // Poll game state every 200 ms
        mPollHandler.postDelayed(mPollRunnable, 200);
    }

    private final Runnable mPollRunnable = new Runnable() {
        @Override public void run() {
            if (mNativeLibLoaded && mOverlay != null) {
                try {
                    boolean inGame = nativeIsGameRunning();
                    int want = inGame ? View.VISIBLE : View.INVISIBLE;
                    if (mOverlay.getVisibility() != want) mOverlay.setVisibility(want);
                } catch (Throwable ignored) {}
            }
            mPollHandler.postDelayed(this, 200);
        }
    };

    // -----------------------------------------------------------------------
    // Dropdown view — renders a vertical list of selectable items
    // -----------------------------------------------------------------------
    static class AADropdownView extends View {
        private final String[] mItems;
        private final float    mAnchorX, mAnchorY;
        private final boolean  mRightAligned;
        private int            mHighlightedIndex = -1;

        private final Paint mBgPaint   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mHlPaint   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final float mItemH;
        private final float mItemW;

        AADropdownView(Context ctx, String[] items, float anchorX, float anchorY, boolean rightAligned) {
            super(ctx);
            mItems        = items;
            mAnchorX      = anchorX;
            mAnchorY      = anchorY;
            mRightAligned = rightAligned;

            DisplayMetrics dm = ctx.getResources().getDisplayMetrics();
            mItemH = 48 * dm.density;
            mItemW = 200 * dm.density;

            mBgPaint.setColor(Color.argb(230, 20, 20, 20));
            mHlPaint.setColor(Color.argb(200, 51, 128, 255));
            mTextPaint.setColor(Color.WHITE);
            mTextPaint.setTextSize(13 * dm.scaledDensity);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float x = mRightAligned ? (mAnchorX - mItemW) : mAnchorX;
            float y = mAnchorY;
            float r = 12 * getResources().getDisplayMetrics().density;
            float pad = 8 * getResources().getDisplayMetrics().density;
            float totalH = mItems.length * mItemH + pad * 2;

            // Background
            canvas.drawRoundRect(new RectF(x, y, x + mItemW, y + totalH), r, r, mBgPaint);

            // Items
            for (int i = 0; i < mItems.length; i++) {
                float iy = y + pad + i * mItemH;
                if (i == mHighlightedIndex) {
                    canvas.drawRoundRect(new RectF(x + pad, iy, x + mItemW - pad, iy + mItemH - 2), 8, 8, mHlPaint);
                }
                String label = mItems[i] != null && !mItems[i].isEmpty() ? mItems[i] : "(empty)";
                float textY = iy + mItemH / 2f - (mTextPaint.descent() + mTextPaint.ascent()) / 2f;
                canvas.drawText(label, x + pad * 2, textY, mTextPaint);
            }
        }

        void updateHighlight(float rawX, float rawY) {
            float x = mRightAligned ? (mAnchorX - mItemW) : mAnchorX;
            float y = mAnchorY;
            float pad = 8 * getResources().getDisplayMetrics().density;
            float totalH = mItems.length * mItemH + pad * 2;

            if (rawX >= x && rawX <= x + mItemW && rawY >= y && rawY <= y + totalH) {
                int idx = (int) ((rawY - y - pad) / mItemH);
                mHighlightedIndex = (idx >= 0 && idx < mItems.length) ? idx : -1;
            } else {
                mHighlightedIndex = -1;
            }
            invalidate();
        }

        int getHighlightedIndex() { return mHighlightedIndex; }
    }

    // -----------------------------------------------------------------------
    // Corner button view
    // -----------------------------------------------------------------------
    static class AACornerButton extends View {
        private static final long   LONG_PRESS_MS       = 450;
        private static final long   DOUBLE_TAP_MS       = 300;
        private static final float  MOVE_SLOP_DP        = 8f;

        private final boolean mRight; // false=left, true=right
        private int mActiveMode = 0;  // 0=none, 1=chat, 2=console

        private final Paint mBgPaint   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mTopPaint  = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mBotPaint  = new Paint(Paint.ANTI_ALIAS_FLAG);

        // Touch state
        private boolean mTracking;
        private float   mDownRawX, mDownRawY;
        private long    mDownTime;
        private boolean mLongPressed;
        private int     mTapCount;
        private long    mLastTapTime;

        // Dropdown
        private AADropdownView mDropdown;

        private final Handler  mHandler = new Handler();
        private final Runnable mLongPressRunnable = new Runnable() {
            @Override public void run() {
                if (mTracking) { mLongPressed = true; showDropdown(); }
            }
        };
        private final Runnable mSingleTapRunnable = new Runnable() {
            @Override public void run() {
                if (mTapCount == 1) fireSingleTap();
                mTapCount = 0;
            }
        };

        AACornerButton(Context ctx, boolean right) {
            super(ctx);
            mRight = right;

            DisplayMetrics dm = ctx.getResources().getDisplayMetrics();
            mBgPaint.setColor(Color.argb(46, 0, 0, 0));       // ~18% alpha black

            mTopPaint.setColor(Color.argb(217, 242, 242, 242));
            mTopPaint.setTextSize(13.0f * dm.scaledDensity);
            mTopPaint.setFakeBoldText(true);
            mTopPaint.setTextAlign(Paint.Align.CENTER);
            mTopPaint.setAntiAlias(true);

            setClickable(true);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight();
            float r = 10 * getResources().getDisplayMetrics().density;
            mBgPaint.setColor(mActiveMode != 0
                ? Color.argb(140, 0, 140, 50)    // green when chat/console active
                : Color.argb(46, 0, 0, 0));       // dark when inactive
            canvas.drawRoundRect(new RectF(2, 2, w - 2, h - 2), r, r, mBgPaint);

            String label = mRight ? "R1" : "L1";
            float cx = w / 2f;
            float cy = h / 2f;
            float textH = mTopPaint.descent() - mTopPaint.ascent();
            canvas.drawText(label, cx, cy + textH * 0.5f - mTopPaint.descent(), mTopPaint);
        }

        void resetState() {
            mActiveMode = 0;
            invalidate();
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            float slop = MOVE_SLOP_DP * getResources().getDisplayMetrics().density;

            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    mTracking    = true;
                    mDownRawX    = e.getRawX();
                    mDownRawY    = e.getRawY();
                    mDownTime    = System.currentTimeMillis();
                    mLongPressed = false;
                    mHandler.postDelayed(mLongPressRunnable, LONG_PRESS_MS);
                    setAlpha(0.7f);
                    return true;

                case MotionEvent.ACTION_MOVE:
                    if (mLongPressed && mDropdown != null) {
                        mDropdown.updateHighlight(e.getRawX(), e.getRawY());
                    } else {
                        float dx = e.getRawX() - mDownRawX;
                        float dy = e.getRawY() - mDownRawY;
                        if (Math.sqrt(dx * dx + dy * dy) > slop) {
                            mHandler.removeCallbacks(mLongPressRunnable);
                            mTracking = false;
                        }
                    }
                    return true;

                case MotionEvent.ACTION_UP:
                case MotionEvent.ACTION_CANCEL:
                    mHandler.removeCallbacks(mLongPressRunnable);
                    setAlpha(1.0f);

                    if (mLongPressed) {
                        int idx = (mDropdown != null) ? mDropdown.getHighlightedIndex() : -1;
                        dismissDropdown();
                        if (idx >= 0) handleDropdownSelection(idx);
                    } else if (mTracking) {
                        float dx = e.getRawX() - mDownRawX;
                        float dy = e.getRawY() - mDownRawY;
                        if (Math.sqrt(dx * dx + dy * dy) < slop) {
                            long now = System.currentTimeMillis();
                            if (mTapCount > 0 && (now - mLastTapTime) < DOUBLE_TAP_MS) {
                                mHandler.removeCallbacks(mSingleTapRunnable);
                                mTapCount = 0;
                                fireDoubleTap();
                            } else {
                                mLastTapTime = now;
                                mTapCount++;
                                mHandler.postDelayed(mSingleTapRunnable, DOUBLE_TAP_MS);
                            }
                        }
                    }
                    mTracking    = false;
                    mLongPressed = false;
                    return true;
            }
            return super.onTouchEvent(e);
        }

        private void fireSingleTap() {
            if (mRight) {
                // R1 single tap: chat (only in networked game)
                try { if (!nativeIsChatAvailable()) return; } catch (Throwable ignored) {}
                try { nativeTriggerPlayerAction("CHAT"); } catch (Throwable ignored) {}
                if (mActiveMode == 1) {
                    mActiveMode = 0;
                    hideKeyboard();
                } else {
                    mActiveMode = 1;
                    showKeyboard();
                }
                invalidate();
            } else {
                // L1 single tap: score
                try { nativeTriggerAction("SCORE"); } catch (Throwable ignored) {}
            }
        }

        private void fireDoubleTap() {
            if (mRight) {
                // R1 double tap: console input. Close chat if active first.
                if (mActiveMode == 1) {
                    try { nativeTriggerPlayerAction("CHAT"); } catch (Throwable ignored) {}
                }
                try { nativeTriggerAction("CONSOLE_INPUT"); } catch (Throwable ignored) {}
                if (mActiveMode == 2) {
                    mActiveMode = 0;
                    hideKeyboard();
                } else {
                    mActiveMode = 2;
                    showKeyboard();
                }
                invalidate();
            } else {
                // L1 double tap: in-game menu
                try { nativeTriggerAction("INGAME_MENU"); } catch (Throwable ignored) {}
                mActiveMode = 0;
                invalidate();
            }
        }

        private void showKeyboard() {
            InputMethodManager imm = (InputMethodManager) getContext()
                .getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.toggleSoftInput(InputMethodManager.SHOW_FORCED, 0);
        }

        private void hideKeyboard() {
            InputMethodManager imm = (InputMethodManager) getContext()
                .getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(getWindowToken(), 0);
        }

        private String[] dropdownItems() {
            if (!mRight) {
                return new String[] {
                    "Touch Mode 1  (steer L/R)",
                    "Touch Mode 2  (swipe)",
                    "Touch Mode 3  (buttons)"
                };
            }
            String[] items = new String[8];
            for (int i = 0; i < 8; i++) {
                try {
                    String s = nativeGetInstantChatString(0, i);
                    items[i] = (s != null && !s.isEmpty()) ? s : "(empty)";
                } catch (Throwable t) {
                    items[i] = "(empty)";
                }
            }
            return items;
        }

        private void showDropdown() {
            if (mDropdown != null) return;

            int[] loc = new int[2];
            getLocationOnScreen(loc);
            float anchorX = loc[0] + (mRight ? getWidth() : 0);
            float anchorY = loc[1] + getHeight() + 4 * getResources().getDisplayMetrics().density;

            ViewGroup root = (ViewGroup) getRootView();
            mDropdown = new AADropdownView(getContext(), dropdownItems(), anchorX, anchorY, mRight);
            root.addView(mDropdown, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT,
                    ViewGroup.LayoutParams.MATCH_PARENT));
        }

        private void dismissDropdown() {
            if (mDropdown == null) return;
            ViewGroup parent = (ViewGroup) mDropdown.getParent();
            if (parent != null) parent.removeView(mDropdown);
            mDropdown = null;
        }

        private void handleDropdownSelection(int idx) {
            if (!mRight) {
                nativeSetTouchMode(idx + 1);  // mode 1, 2, or 3
            } else {
                int[] fkeys = { SC_F1, SC_F2, SC_F3, SC_F4, SC_F5, SC_F6, SC_F7, SC_F8 };
                if (idx < fkeys.length) nativeInjectSdlKey(fkeys[idx]);
            }
        }
    }

    // -----------------------------------------------------------------------
    // Gyro toggle button — tap to enable/disable accelerometer camera look
    // -----------------------------------------------------------------------
    class AAGyroButton extends View {
        private boolean mEnabled = false;

        private final Paint mBgPaint   = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mTopPaint  = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mBotPaint  = new Paint(Paint.ANTI_ALIAS_FLAG);

        AAGyroButton(Context ctx) {
            super(ctx);
            DisplayMetrics dm = ctx.getResources().getDisplayMetrics();
            mBgPaint.setColor(Color.argb(46, 0, 0, 0));
            mTopPaint.setTextSize(11.0f * dm.scaledDensity);
            mTopPaint.setFakeBoldText(true);
            mTopPaint.setTextAlign(Paint.Align.CENTER);
            mTopPaint.setAntiAlias(true);
            setClickable(true);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight();
            float r = 8 * getResources().getDisplayMetrics().density;

            mBgPaint.setColor(mEnabled ? Color.argb(90, 0, 180, 60)
                                        : Color.argb(46, 0, 0, 0));
            canvas.drawRoundRect(new RectF(2, 2, w - 2, h - 2), r, r, mBgPaint);

            mTopPaint.setColor(mEnabled ? Color.argb(220, 80, 255, 80)
                                        : Color.argb(191, 230, 230, 230));
            float cx = w / 2f, cy = h / 2f;
            float textH = mTopPaint.descent() - mTopPaint.ascent();
            canvas.drawText("L2", cx, cy + textH * 0.5f - mTopPaint.descent(), mTopPaint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    setAlpha(0.7f);
                    return true;
                case MotionEvent.ACTION_UP:
                    setAlpha(1.0f);
                    mEnabled = !mEnabled;
                    invalidate();
                    onGyroToggled(mEnabled);
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    setAlpha(1.0f);
                    return true;
            }
            return super.onTouchEvent(e);
        }
    }

    // -----------------------------------------------------------------------
    // Camera button — tap to cycle through camera modes (SWITCH_VIEW action)
    // -----------------------------------------------------------------------
    static class AACameraButton extends View {
        private final Paint mBgPaint  = new Paint(Paint.ANTI_ALIAS_FLAG);
        private final Paint mIconPaint = new Paint(Paint.ANTI_ALIAS_FLAG);

        AACameraButton(Context ctx) {
            super(ctx);
            DisplayMetrics dm = ctx.getResources().getDisplayMetrics();
            mBgPaint.setColor(Color.argb(46, 0, 0, 0));
            mIconPaint.setColor(Color.argb(191, 230, 230, 230));
            mIconPaint.setTextSize(11.0f * dm.scaledDensity);
            mIconPaint.setFakeBoldText(true);
            mIconPaint.setTextAlign(Paint.Align.CENTER);
            mIconPaint.setAntiAlias(true);
            setClickable(true);
        }

        @Override
        protected void onDraw(Canvas canvas) {
            float w = getWidth(), h = getHeight();
            float r = 8 * getResources().getDisplayMetrics().density;
            canvas.drawRoundRect(new RectF(2, 2, w - 2, h - 2), r, r, mBgPaint);
            float cx = w / 2f, cy = h / 2f;
            float textH = mIconPaint.descent() - mIconPaint.ascent();
            canvas.drawText("R2", cx, cy + textH * 0.5f - mIconPaint.descent(), mIconPaint);
        }

        @Override
        public boolean onTouchEvent(MotionEvent e) {
            switch (e.getActionMasked()) {
                case MotionEvent.ACTION_DOWN:
                    setAlpha(0.7f);
                    return true;
                case MotionEvent.ACTION_UP:
                    setAlpha(1.0f);
                    nativeSwitchCameraView();
                    return true;
                case MotionEvent.ACTION_CANCEL:
                    setAlpha(1.0f);
                    return true;
            }
            return super.onTouchEvent(e);
        }
    }
}
