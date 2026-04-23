# Baseline Test Recordings

This directory contains canonical recordings for regression testing.

## Creating Baseline Recordings

To create a baseline recording:

```bash
# Start game with recording enabled
./armagetronad_main --record baseline_v1

# Play for 30-60 seconds with various inputs:
# - Keyboard turns
# - Menu navigation
# - Chat messages (if applicable)
# - Exit cleanly

# Recording will be saved to ~/.armagetronad/var/
```

## Required Recordings

| File | Purpose | Duration |
|------|---------|----------|
| baseline_gameplay.rec | Basic gameplay (turns, collisions) | 60s |
| baseline_menu.rec | Menu navigation | 30s |
| baseline_chat.rec | Chat input handling | 30s |

## Playback Testing

```bash
# Test playback works
./armagetronad_main --playback baseline_gameplay

# Verify no errors/crashes during playback
```

## Version Compatibility

These recordings are used to verify backward compatibility after event system abstraction (Sprint 3). After Sprint 3, playback of these recordings MUST still work.

## Notes

- Recordings are DEBUG recordings (not demo files)
- Recording format serializes SDL_Event structures directly
- Keep recordings small (<1MB) for CI/CD efficiency
