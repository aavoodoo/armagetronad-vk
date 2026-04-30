#ifndef LUAPP_HPP_INCLUDED
#define LUAPP_HPP_INCLUDED

#include <cassert>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <iterator>
#include <utility>

extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}

namespace luapp {

// ============================================================================
// Layer 0: Strong Types
// ============================================================================

struct idx {
    int value;
    constexpr explicit idx(int v) noexcept : value(v) {}
    [[nodiscard]] constexpr int raw() const noexcept { return value; }
};

struct abs_idx {
    int value;
    constexpr explicit abs_idx(int v) noexcept : value(v) {
        assert((v > 0 || v <= LUA_REGISTRYINDEX) && "abs_idx must be positive or a pseudo-index");
    }
    [[nodiscard]] constexpr int raw() const noexcept { return value; }
    constexpr operator idx() const noexcept { return idx{value}; }
};

struct rel_idx {
    int value;
    constexpr explicit rel_idx(int v) noexcept : value(v) {
        assert((v < 0 && v > LUA_REGISTRYINDEX) && "rel_idx must be negative and not a pseudo-index");
    }
    [[nodiscard]] constexpr int raw() const noexcept { return value; }
    constexpr operator idx() const noexcept { return idx{value}; }
};

struct result_count {
    int value;
    constexpr explicit result_count(int v) noexcept : value(v) {}
    [[nodiscard]] constexpr int raw() const noexcept { return value; }
    [[nodiscard]] static constexpr result_count multi() noexcept {
        return result_count{LUA_MULTRET};
    }
};

struct arg_count {
    int value;
    constexpr explicit arg_count(int v) noexcept : value(v) {}
    [[nodiscard]] constexpr int raw() const noexcept { return value; }
};

struct upvalue_index {
    int value;
    constexpr explicit upvalue_index(int v) noexcept : value(v) {}
    [[nodiscard]] constexpr int raw() const noexcept { return value; }
    [[nodiscard]] constexpr int lua_index() const noexcept {
        return LUA_GLOBALSINDEX - value;
    }
};

namespace literals {

consteval idx operator""_idx(unsigned long long v) {
    return idx{static_cast<int>(v)};
}

consteval result_count operator""_rc(unsigned long long v) {
    return result_count{static_cast<int>(v)};
}

consteval arg_count operator""_ac(unsigned long long v) {
    return arg_count{static_cast<int>(v)};
}

} // namespace literals

// ============================================================================
// Layer 0: Strong Enums
// ============================================================================

enum class type : int {
    none          = LUA_TNONE,
    nil           = LUA_TNIL,
    boolean       = LUA_TBOOLEAN,
    lightuserdata = LUA_TLIGHTUSERDATA,
    number        = LUA_TNUMBER,
    string        = LUA_TSTRING,
    table         = LUA_TTABLE,
    function      = LUA_TFUNCTION,
    userdata      = LUA_TUSERDATA,
    thread        = LUA_TTHREAD,
};

enum class status : int {
    ok      = LUA_OK,
    yield   = LUA_YIELD,
    runtime = LUA_ERRRUN,
    syntax  = LUA_ERRSYNTAX,
    memory  = LUA_ERRMEM,
    error   = LUA_ERRERR,
    file    = LUA_ERRFILE,
};

enum class gc_option : int {
    stop         = LUA_GCSTOP,
    restart      = LUA_GCRESTART,
    collect      = LUA_GCCOLLECT,
    count        = LUA_GCCOUNT,
    count_bytes  = LUA_GCCOUNTB,
    step         = LUA_GCSTEP,
    set_pause    = LUA_GCSETPAUSE,
    set_step_mul = LUA_GCSETSTEPMUL,
    is_running   = LUA_GCISRUNNING,
};

// Pseudo-indices as typed constants
inline constexpr idx registry_index{LUA_REGISTRYINDEX};
inline constexpr idx environ_index{LUA_ENVIRONINDEX};
inline constexpr idx globals_index{LUA_GLOBALSINDEX};

// ============================================================================
// Layer 0: stack_traits — type mapping between C++ and Lua stack
// ============================================================================

template <typename T, typename = void>
struct stack_traits {
    static_assert(sizeof(T) == 0, "No stack_traits specialization for this type");
};

template <>
struct stack_traits<bool> {
    static bool get(lua_State* L, int i) noexcept {
        return lua_toboolean(L, i) != 0;
    }
    static void push(lua_State* L, bool v) noexcept {
        lua_pushboolean(L, v);
    }
};

template <>
struct stack_traits<lua_Integer> {
    static lua_Integer get(lua_State* L, int i) noexcept {
        return lua_tointeger(L, i);
    }
    static void push(lua_State* L, lua_Integer v) noexcept {
        lua_pushinteger(L, v);
    }
};

template <>
struct stack_traits<int, std::enable_if_t<!std::is_same_v<int, lua_Integer>>> {
    static int get(lua_State* L, int i) noexcept {
        return static_cast<int>(lua_tointeger(L, i));
    }
    static void push(lua_State* L, int v) noexcept {
        lua_pushinteger(L, static_cast<lua_Integer>(v));
    }
};

template <>
struct stack_traits<lua_Number> {
    static lua_Number get(lua_State* L, int i) noexcept {
        return lua_tonumber(L, i);
    }
    static void push(lua_State* L, lua_Number v) noexcept {
        lua_pushnumber(L, v);
    }
};

template <>
struct stack_traits<float, std::enable_if_t<!std::is_same_v<float, lua_Number>>> {
    static float get(lua_State* L, int i) noexcept {
        return static_cast<float>(lua_tonumber(L, i));
    }
    static void push(lua_State* L, float v) noexcept {
        lua_pushnumber(L, static_cast<lua_Number>(v));
    }
};

template <>
struct stack_traits<const char*> {
    static const char* get(lua_State* L, int i) noexcept {
        return lua_tostring(L, i);
    }
    static void push(lua_State* L, const char* v) noexcept {
        lua_pushstring(L, v);
    }
};

template <>
struct stack_traits<std::string_view> {
    static std::string_view get(lua_State* L, int i) noexcept {
        std::size_t len = 0;
        const char* s = lua_tolstring(L, i, &len);
        return {s, len};
    }
    static void push(lua_State* L, std::string_view v) noexcept {
        lua_pushlstring(L, v.data(), v.size());
    }
};

template <>
struct stack_traits<lua_CFunction> {
    static lua_CFunction get(lua_State* L, int i) noexcept {
        return lua_tocfunction(L, i);
    }
    static void push(lua_State* L, lua_CFunction v) noexcept {
        lua_pushcfunction(L, v);
    }
};

template <>
struct stack_traits<void*> {
    static void* get(lua_State* L, int i) noexcept {
        return lua_touserdata(L, i);
    }
    static void push(lua_State* L, void* v) noexcept {
        lua_pushlightuserdata(L, v);
    }
};

template <>
struct stack_traits<std::nullptr_t> {
    static void push(lua_State* L, std::nullptr_t) noexcept {
        lua_pushnil(L);
    }
};

// ============================================================================
// Layer 0: multi_return — multiple return values from wrapped functions
// ============================================================================

template <typename... Ts>
struct multi_return {
    std::tuple<Ts...> values;
};

template <typename... Ts>
[[nodiscard]] multi_return<std::decay_t<Ts>...> multi(Ts&&... vals) {
    return {std::tuple{std::forward<Ts>(vals)...}};
}

// ============================================================================
// detail:: — metaprogramming utilities
// ============================================================================

namespace detail {

template <typename F>
struct function_traits;

template <typename R, typename... Args>
struct function_traits<R(*)(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename R, typename... Args>
struct function_traits<R(Args...)> {
    using return_type = R;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename F>
struct member_function_traits;

template <typename R, typename C, typename... Args>
struct member_function_traits<R(C::*)(Args...)> {
    using return_type = R;
    using class_type = C;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename R, typename C, typename... Args>
struct member_function_traits<R(C::*)(Args...) const> {
    using return_type = R;
    using class_type = C;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename R, typename C, typename... Args>
struct member_function_traits<R(C::*)(Args...) noexcept> {
    using return_type = R;
    using class_type = C;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename R, typename C, typename... Args>
struct member_function_traits<R(C::*)(Args...) const noexcept> {
    using return_type = R;
    using class_type = C;
    using args_tuple = std::tuple<std::decay_t<Args>...>;
    static constexpr std::size_t arity = sizeof...(Args);
};

template <typename T>
concept member_function_pointer = std::is_member_function_pointer_v<T>;

template <typename T>
concept free_function_pointer = std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>;

template <typename ArgsTuple, std::size_t... Is>
auto extract_args_impl(lua_State* L, int offset, std::index_sequence<Is...>) {
    return std::tuple{
        stack_traits<std::tuple_element_t<Is, ArgsTuple>>::get(L, static_cast<int>(Is) + offset)...
    };
}

template <typename ArgsTuple>
auto extract_args(lua_State* L, int offset = 1) {
    constexpr auto arity = std::tuple_size_v<ArgsTuple>;
    return extract_args_impl<ArgsTuple>(L, offset, std::make_index_sequence<arity>{});
}

template <typename Tuple, std::size_t... Is>
void push_tuple(lua_State* L, const Tuple& t, std::index_sequence<Is...>) {
    (stack_traits<std::tuple_element_t<Is, Tuple>>::push(L, std::get<Is>(t)), ...);
}

template <typename T>
struct is_multi_return : std::false_type {};

template <typename... Ts>
struct is_multi_return<multi_return<Ts...>> : std::true_type {};

template <typename T>
inline constexpr bool is_multi_return_v = is_multi_return<T>::value;

// Helpers for create_map: process key-value pairs recursively
inline void set_map_fields(lua_State*, int) noexcept {} // base case

template <typename V, typename... Rest>
void set_map_fields(lua_State* L, int table, const char* key, V&& val, Rest&&... rest) noexcept {
    stack_traits<std::decay_t<V>>::push(L, std::forward<V>(val));
    lua_setfield(L, table, key);
    set_map_fields(L, table, std::forward<Rest>(rest)...);
}

// Helper for create_array: push values with sequential integer keys
template <typename... Args, std::size_t... Is>
void set_array_elements(lua_State* L, int table, std::index_sequence<Is...>, Args&&... args) noexcept {
    ((stack_traits<std::decay_t<Args>>::push(L, std::forward<Args>(args)),
      lua_rawseti(L, table, static_cast<int>(Is) + 1)), ...);
}

// Helper for multi-get: extract consecutive values into a tuple
template <typename... Ts, std::size_t... Is>
std::tuple<Ts...> get_multi_impl(lua_State* L, int base, std::index_sequence<Is...>) noexcept {
    return {stack_traits<Ts>::get(L, base + static_cast<int>(Is))...};
}

} // namespace detail

// Forward declarations for container views (Layer 7)
class table_array;
class table_map;

// ============================================================================
// Layer 1: stack_element — read-only proxy for one stack slot
// ============================================================================

class stack_element {
    lua_State* L_;
    int pos_;

public:
    constexpr stack_element(lua_State* L, int pos) noexcept : L_(L), pos_(pos) {}

    [[nodiscard]] idx position() const noexcept { return idx{pos_}; }

    [[nodiscard]] luapp::type type() const noexcept {
        return static_cast<luapp::type>(lua_type(L_, pos_));
    }

    template <typename T>
    [[nodiscard]] T as() const noexcept {
        return stack_traits<T>::get(L_, pos_);
    }
};

// ============================================================================
// Layer 1: state_view — non-owning lua_State* wrapper
// ============================================================================

class state_view {
protected:
    lua_State* L_;

public:
    constexpr state_view(lua_State* L) noexcept : L_(L) {}

    [[nodiscard]] lua_State* raw() const noexcept { return L_; }
    explicit operator lua_State*() const noexcept { return L_; }

    // --- Stack manipulation ---

    [[nodiscard]] int top() const noexcept {
        return lua_gettop(L_);
    }

    void set_top(idx i) noexcept {
        lua_settop(L_, i.raw());
    }

    void pop(int n = 1) noexcept {
        lua_pop(L_, n);
    }

    void push_value(idx i) noexcept {
        lua_pushvalue(L_, i.raw());
    }

    void remove(idx i) noexcept {
        lua_remove(L_, i.raw());
    }

    void insert(idx i) noexcept {
        lua_insert(L_, i.raw());
    }

    void replace(idx i) noexcept {
        lua_replace(L_, i.raw());
    }

    [[nodiscard]] int abs_index(idx i) const noexcept {
        int v = i.raw();
        if (v > 0 || v <= LUA_REGISTRYINDEX)
            return v;
        return lua_gettop(L_) + v + 1;
    }

    // --- Index type conversions ---

    [[nodiscard]] abs_idx to_abs(idx i) const noexcept {
        return abs_idx{abs_index(i)};
    }

    [[nodiscard]] abs_idx to_abs(rel_idx i) const noexcept {
        return abs_idx{lua_gettop(L_) + i.raw() + 1};
    }

    [[nodiscard]] constexpr abs_idx to_abs(abs_idx i) const noexcept {
        return i; // already absolute, no-op
    }

    [[nodiscard]] rel_idx to_rel(abs_idx i) const noexcept {
        return rel_idx{i.raw() - lua_gettop(L_) - 1};
    }

    [[nodiscard]] bool check_stack(int extra) noexcept {
        return lua_checkstack(L_, extra) != 0;
    }

    // --- Type queries ---

    [[nodiscard]] type type_of(idx i) const noexcept {
        return static_cast<type>(lua_type(L_, i.raw()));
    }

    [[nodiscard]] const char* type_name(type t) const noexcept {
        return lua_typename(L_, static_cast<int>(t));
    }

    [[nodiscard]] bool is_nil(idx i) const noexcept {
        return lua_type(L_, i.raw()) == LUA_TNIL;
    }

    [[nodiscard]] bool is_none(idx i) const noexcept {
        return lua_type(L_, i.raw()) == LUA_TNONE;
    }

    [[nodiscard]] bool is_none_or_nil(idx i) const noexcept {
        return lua_type(L_, i.raw()) <= 0;
    }

    [[nodiscard]] bool is_boolean(idx i) const noexcept {
        return lua_type(L_, i.raw()) == LUA_TBOOLEAN;
    }

    [[nodiscard]] bool is_number(idx i) const noexcept {
        return lua_isnumber(L_, i.raw()) != 0;
    }

    [[nodiscard]] bool is_string(idx i) const noexcept {
        return lua_isstring(L_, i.raw()) != 0;
    }

    [[nodiscard]] bool is_table(idx i) const noexcept {
        return lua_type(L_, i.raw()) == LUA_TTABLE;
    }

    [[nodiscard]] bool is_function(idx i) const noexcept {
        return lua_type(L_, i.raw()) == LUA_TFUNCTION;
    }

    [[nodiscard]] bool is_cfunction(idx i) const noexcept {
        return lua_iscfunction(L_, i.raw()) != 0;
    }

    [[nodiscard]] bool is_userdata(idx i) const noexcept {
        return lua_isuserdata(L_, i.raw()) != 0;
    }

    [[nodiscard]] bool is_lightuserdata(idx i) const noexcept {
        return lua_type(L_, i.raw()) == LUA_TLIGHTUSERDATA;
    }

    [[nodiscard]] bool is_thread(idx i) const noexcept {
        return lua_type(L_, i.raw()) == LUA_TTHREAD;
    }

    // --- Push operations (overloaded) ---

    void push(std::nullptr_t) noexcept { lua_pushnil(L_); }
    void push(bool v) noexcept { lua_pushboolean(L_, v); }
    void push(lua_Integer v) noexcept { lua_pushinteger(L_, v); }
    void push(lua_Number v) noexcept { lua_pushnumber(L_, v); }
    void push(const char* s) noexcept { lua_pushstring(L_, s); }
    void push(std::string_view s) noexcept { lua_pushlstring(L_, s.data(), s.size()); }
    void push(lua_CFunction fn) noexcept { lua_pushcfunction(L_, fn); }
    void push(lua_CFunction fn, int nupvalues) noexcept { lua_pushcclosure(L_, fn, nupvalues); }
    void push(void* p) noexcept { lua_pushlightuserdata(L_, p); }

    // Variadic push: push multiple values in one call
    template <typename T, typename... Ts>
        requires (sizeof...(Ts) > 0)
    void push(T&& first, Ts&&... rest) noexcept {
        push(std::forward<T>(first));
        (push(std::forward<Ts>(rest)), ...);
    }

    // --- Get operations (typed) ---

    template <typename T>
    [[nodiscard]] T get(idx i) const noexcept {
        return stack_traits<T>::get(L_, i.raw());
    }

    // Multi-get: extract consecutive stack values into a tuple for structured bindings
    template <typename T1, typename T2, typename... Ts>
    [[nodiscard]] std::tuple<T1, T2, Ts...> get(idx start) const noexcept {
        return detail::get_multi_impl<T1, T2, Ts...>(
            L_, start.raw(), std::make_index_sequence<2 + sizeof...(Ts)>{});
    }

    // --- Table operations ---

    void get_table(idx i) noexcept { lua_gettable(L_, i.raw()); }
    void set_table(idx i) noexcept { lua_settable(L_, i.raw()); }
    void get_field(idx i, const char* k) noexcept { lua_getfield(L_, i.raw(), k); }
    void set_field(idx i, const char* k) noexcept { lua_setfield(L_, i.raw(), k); }
    void raw_get(idx i) noexcept { lua_rawget(L_, i.raw()); }
    void raw_set(idx i) noexcept { lua_rawset(L_, i.raw()); }
    void raw_geti(idx i, int n) noexcept { lua_rawgeti(L_, i.raw(), n); }
    void raw_seti(idx i, int n) noexcept { lua_rawseti(L_, i.raw(), n); }
    void create_table(int narr = 0, int nrec = 0) noexcept { lua_createtable(L_, narr, nrec); }
    void new_table() noexcept { lua_newtable(L_); }

    // --- Table convenience (sugar) ---

    // set_field with value: push + setfield in one call
    template <typename T>
    void set_field(idx i, const char* k, T&& value) noexcept {
        int abs = abs_index(i);
        stack_traits<std::decay_t<T>>::push(L_, std::forward<T>(value));
        lua_setfield(L_, abs, k);
    }

    // raw_seti with value: push + rawseti in one call
    template <typename T>
    void raw_seti(idx i, int n, T&& value) noexcept {
        int abs = abs_index(i);
        stack_traits<std::decay_t<T>>::push(L_, std::forward<T>(value));
        lua_rawseti(L_, abs, n);
    }

    // get_field + extract + pop in one call
    template <typename T>
    [[nodiscard]] T get_field(idx i, const char* k) noexcept {
        lua_getfield(L_, i.raw(), k);
        T val = stack_traits<T>::get(L_, -1);
        lua_pop(L_, 1);
        return val;
    }

    // get_field + type query + pop in one call
    [[nodiscard]] type field_type(idx i, const char* k) noexcept {
        lua_getfield(L_, i.raw(), k);
        auto t = static_cast<type>(lua_type(L_, -1));
        lua_pop(L_, 1);
        return t;
    }

    // Create a table with key-value pairs: create_map("k1", v1, "k2", v2, ...)
    template <typename... Args>
    void create_map(Args&&... args) noexcept {
        static_assert(sizeof...(Args) % 2 == 0, "create_map requires key-value pairs");
        static_assert(sizeof...(Args) >= 2, "create_map requires at least one key-value pair");
        constexpr int n = sizeof...(Args) / 2;
        lua_createtable(L_, 0, n);
        detail::set_map_fields(L_, lua_gettop(L_), std::forward<Args>(args)...);
    }

    // Create an array table: create_array(v1, v2, v3, ...)
    template <typename... Args>
    void create_array(Args&&... args) noexcept {
        static_assert(sizeof...(Args) >= 1, "create_array requires at least one value");
        constexpr int n = sizeof...(Args);
        lua_createtable(L_, n, 0);
        detail::set_array_elements(L_, lua_gettop(L_), std::make_index_sequence<n>{},
                                   std::forward<Args>(args)...);
    }

    // --- Global access ---

    void get_global(const char* name) noexcept {
        lua_getfield(L_, LUA_GLOBALSINDEX, name);
    }

    // get_global + extract + pop in one call
    template <typename T>
    [[nodiscard]] T get_global(const char* name) noexcept {
        lua_getfield(L_, LUA_GLOBALSINDEX, name);
        T val = stack_traits<T>::get(L_, -1);
        lua_pop(L_, 1);
        return val;
    }

    void set_global(const char* name) noexcept {
        lua_setfield(L_, LUA_GLOBALSINDEX, name);
    }

    template <typename T>
    void set_global(const char* name, T&& value) noexcept {
        stack_traits<std::decay_t<T>>::push(L_, std::forward<T>(value));
        lua_setfield(L_, LUA_GLOBALSINDEX, name);
    }

    // Batch-register global C functions
    void register_funcs(std::initializer_list<std::pair<const char*, lua_CFunction>> funcs) noexcept {
        for (auto [name, fn] : funcs) {
            lua_pushcfunction(L_, fn);
            lua_setfield(L_, LUA_GLOBALSINDEX, name);
        }
    }

    // --- Call operations ---

    void call(arg_count nargs, result_count nresults) noexcept {
        lua_call(L_, nargs.raw(), nresults.raw());
    }

    [[nodiscard]] status pcall(arg_count nargs, result_count nresults,
                               idx errfunc = idx{0}) noexcept {
        return static_cast<status>(lua_pcall(L_, nargs.raw(), nresults.raw(), errfunc.raw()));
    }

    // Call a global Lua function by name, push args, pcall, extract result, pop
    // Returns {status, result} — result is default-constructed if status != ok
    template <typename Ret, typename... Args>
        requires (!std::is_void_v<Ret>)
    [[nodiscard]] std::pair<status, Ret> call_global(const char* name, Args&&... args) noexcept {
        lua_getfield(L_, LUA_GLOBALSINDEX, name);
        (stack_traits<std::decay_t<Args>>::push(L_, std::forward<Args>(args)), ...);
        auto s = static_cast<status>(lua_pcall(L_, sizeof...(Args), 1, 0));
        if (s == status::ok) {
            Ret val = stack_traits<Ret>::get(L_, -1);
            lua_pop(L_, 1);
            return {s, val};
        }
        lua_pop(L_, 1); // pop error message
        return {s, Ret{}};
    }

    // call_global<void> specialization — just calls, no result extraction
    template <typename Ret, typename... Args>
        requires std::is_void_v<Ret>
    [[nodiscard]] status call_global(const char* name, Args&&... args) noexcept {
        lua_getfield(L_, LUA_GLOBALSINDEX, name);
        (stack_traits<std::decay_t<Args>>::push(L_, std::forward<Args>(args)), ...);
        auto s = static_cast<status>(lua_pcall(L_, sizeof...(Args), 0, 0));
        if (s != status::ok)
            lua_pop(L_, 1);
        return s;
    }

    // --- Metatable ---

    [[nodiscard]] bool get_metatable(idx i) noexcept { return lua_getmetatable(L_, i.raw()) != 0; }
    void set_metatable(idx i) noexcept { lua_setmetatable(L_, i.raw()); }

    [[nodiscard]] bool new_metatable(const char* tname) noexcept {
        return luaL_newmetatable(L_, tname) != 0;
    }

    // --- Userdata ---

    [[nodiscard]] void* new_userdata(std::size_t size) noexcept {
        return lua_newuserdata(L_, size);
    }

    template <typename T, typename... Args>
    [[nodiscard]] T& emplace_userdata(Args&&... args) noexcept {
        void* mem = lua_newuserdata(L_, sizeof(T));
        return *new (mem) T(std::forward<Args>(args)...);
    }

    // Typed userdata check (defined out-of-line after userdata_traits)
    template <typename T>
    [[nodiscard]] inline T& check_udata(idx i);

    template <typename T>
    [[nodiscard]] inline T* to_udata(idx i) noexcept;

    // --- Auxiliary library (luaL_*) ---

    void open_libs() noexcept { luaL_openlibs(L_); }

    [[nodiscard]] status load_string(const char* s) noexcept {
        return static_cast<status>(luaL_loadstring(L_, s));
    }

    [[nodiscard]] status load_file(const char* path) noexcept {
        return static_cast<status>(luaL_loadfile(L_, path));
    }

    [[nodiscard]] status do_string(const char* s) noexcept {
        return static_cast<status>(luaL_dostring(L_, s));
    }

    [[nodiscard]] status do_file(const char* path) noexcept {
        return static_cast<status>(luaL_dofile(L_, path));
    }

    // --- Argument checking (for C functions exposed to Lua) ---

    [[nodiscard]] lua_Number check_number(idx arg) const { return luaL_checknumber(L_, arg.raw()); }
    [[nodiscard]] lua_Integer check_integer(idx arg) const { return luaL_checkinteger(L_, arg.raw()); }
    [[nodiscard]] const char* check_string(idx arg) const { return luaL_checkstring(L_, arg.raw()); }
    [[nodiscard]] const char* check_lstring(idx arg, std::size_t* len) const { return luaL_checklstring(L_, arg.raw(), len); }
    void check_type(idx arg, type t) const { luaL_checktype(L_, arg.raw(), static_cast<int>(t)); }
    void check_any(idx arg) const { luaL_checkany(L_, arg.raw()); }

    [[nodiscard]] lua_Number opt_number(idx arg, lua_Number def) const { return luaL_optnumber(L_, arg.raw(), def); }
    [[nodiscard]] lua_Integer opt_integer(idx arg, lua_Integer def) const { return luaL_optinteger(L_, arg.raw(), def); }
    [[nodiscard]] const char* opt_string(idx arg, const char* def) const { return luaL_optstring(L_, arg.raw(), def); }

    // --- Reference system ---

    [[nodiscard]] int ref(idx table_i = registry_index) noexcept {
        return luaL_ref(L_, table_i.raw());
    }

    void unref(int r, idx table_i = registry_index) noexcept {
        luaL_unref(L_, table_i.raw(), r);
    }

    // --- GC ---

    int gc(gc_option opt, int data = 0) noexcept {
        return lua_gc(L_, static_cast<int>(opt), data);
    }

    // --- Comparison and length (Lua 5.1 API) ---

    [[nodiscard]] std::size_t obj_len(idx i) const noexcept { return lua_objlen(L_, i.raw()); }
    [[nodiscard]] bool equal(idx a, idx b) const noexcept { return lua_equal(L_, a.raw(), b.raw()) != 0; }
    [[nodiscard]] bool raw_equal(idx a, idx b) const noexcept { return lua_rawequal(L_, a.raw(), b.raw()) != 0; }
    [[nodiscard]] bool less_than(idx a, idx b) const noexcept { return lua_lessthan(L_, a.raw(), b.raw()) != 0; }

    // --- Table traversal ---

    [[nodiscard]] bool next(idx table_i) noexcept { return lua_next(L_, table_i.raw()) != 0; }

    // --- String operations ---

    void concat(int n) noexcept { lua_concat(L_, n); }

    // --- Error ---

    [[noreturn]] int error() noexcept {
        lua_error(L_);
        __builtin_unreachable();
    }

    // --- Misc ---

    [[nodiscard]] lua_State* new_thread() noexcept { return lua_newthread(L_); }
    [[nodiscard]] status get_status() const noexcept { return static_cast<status>(lua_status(L_)); }

    void xmove(state_view to, int n) noexcept {
        lua_xmove(L_, to.L_, n);
    }

    // --- Stack as read-only container (1-based indexing) ---

    class iterator {
        lua_State* L_;
        int pos_;

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = stack_element;
        using difference_type   = int;
        using pointer           = void;
        using reference         = stack_element;

        constexpr iterator(lua_State* L, int pos) noexcept : L_(L), pos_(pos) {}

        [[nodiscard]] stack_element operator*() const noexcept { return {L_, pos_}; }
        [[nodiscard]] stack_element operator[](int n) const noexcept { return {L_, pos_ + n}; }

        iterator& operator++() noexcept { ++pos_; return *this; }
        iterator  operator++(int) noexcept { auto tmp = *this; ++pos_; return tmp; }
        iterator& operator--() noexcept { --pos_; return *this; }
        iterator  operator--(int) noexcept { auto tmp = *this; --pos_; return tmp; }

        iterator& operator+=(int n) noexcept { pos_ += n; return *this; }
        iterator& operator-=(int n) noexcept { pos_ -= n; return *this; }

        friend iterator operator+(iterator it, int n) noexcept { return {it.L_, it.pos_ + n}; }
        friend iterator operator+(int n, iterator it) noexcept { return {it.L_, it.pos_ + n}; }
        friend iterator operator-(iterator it, int n) noexcept { return {it.L_, it.pos_ - n}; }
        friend int operator-(iterator a, iterator b) noexcept { return a.pos_ - b.pos_; }

        friend bool operator==(iterator a, iterator b) noexcept { return a.pos_ == b.pos_; }
        friend auto operator<=>(iterator a, iterator b) noexcept { return a.pos_ <=> b.pos_; }
    };

    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(lua_gettop(L_));
    }

    [[nodiscard]] stack_element operator[](int i) const noexcept {
        return stack_element{L_, i};
    }

    [[nodiscard]] iterator begin() const noexcept { return {L_, 1}; }
    [[nodiscard]] iterator end() const noexcept { return {L_, lua_gettop(L_) + 1}; }

    // --- Table container views (defined out-of-line after Layer 7) ---

    [[nodiscard]] inline table_array arr(idx table_i) noexcept;
    [[nodiscard]] inline table_array arr(abs_idx table_i) noexcept;
    [[nodiscard]] inline table_map map(idx table_i) noexcept;
    [[nodiscard]] inline table_map map(abs_idx table_i) noexcept;
};

// ============================================================================
// Layer 2: state — owning RAII wrapper
// ============================================================================

class state : public state_view {
public:
    state() : state_view(luaL_newstate()) {}

    explicit state(lua_Alloc alloc, void* ud = nullptr)
        : state_view(lua_newstate(alloc, ud)) {}

    ~state() {
        if (L_) lua_close(L_);
    }

    state(state&& other) noexcept : state_view(other.L_) {
        other.L_ = nullptr;
    }

    state& operator=(state&& other) noexcept {
        if (this != &other) {
            if (L_) lua_close(L_);
            L_ = other.L_;
            other.L_ = nullptr;
        }
        return *this;
    }

    state(const state&) = delete;
    state& operator=(const state&) = delete;
};

// ============================================================================
// Layer 3: stack_guard — RAII stack cleanup
// ============================================================================

class stack_guard {
    lua_State* L_;
    int top_;

public:
    explicit stack_guard(state_view L, int delta = 0) noexcept
        : L_(L.raw()), top_(lua_gettop(L.raw()) + delta) {}

    ~stack_guard() noexcept { lua_settop(L_, top_); }

    stack_guard(const stack_guard&) = delete;
    stack_guard& operator=(const stack_guard&) = delete;
};

// ============================================================================
// Layer 4: wrap<> — compile-time function binding
// ============================================================================

namespace detail {

template <auto Func>
    requires free_function_pointer<decltype(Func)>
int invoke_free(lua_State* L) noexcept {
    using traits = function_traits<decltype(Func)>;
    using ret_t = typename traits::return_type;
    using args_t = typename traits::args_tuple;

    auto args = extract_args<args_t>(L, 1);

    if constexpr (std::is_void_v<ret_t>) {
        std::apply(Func, args);
        return 0;
    } else if constexpr (is_multi_return_v<ret_t>) {
        auto mr = std::apply(Func, args);
        constexpr auto n = std::tuple_size_v<decltype(mr.values)>;
        push_tuple(L, mr.values, std::make_index_sequence<n>{});
        return static_cast<int>(n);
    } else {
        auto result = std::apply(Func, args);
        stack_traits<ret_t>::push(L, result);
        return 1;
    }
}

} // namespace detail

template <auto Func>
    requires detail::free_function_pointer<decltype(Func)>
[[nodiscard]] consteval lua_CFunction wrap() noexcept {
    return &detail::invoke_free<Func>;
}

// ============================================================================
// Layer 5: Class binding
// ============================================================================

template <typename T>
struct userdata_traits {
    static constexpr const char* name = nullptr;
};

template <typename T>
[[nodiscard]] T* to_userdata(lua_State* L, idx i) noexcept {
    return static_cast<T*>(lua_touserdata(L, i.raw()));
}

template <typename T>
[[nodiscard]] T& check_userdata(lua_State* L, idx i) {
    return *static_cast<T*>(luaL_checkudata(L, i.raw(), userdata_traits<T>::name));
}

// Out-of-line definitions for state_view userdata methods
template <typename T>
T& state_view::check_udata(idx i) {
    return *static_cast<T*>(luaL_checkudata(L_, i.raw(), userdata_traits<T>::name));
}

template <typename T>
T* state_view::to_udata(idx i) noexcept {
    return static_cast<T*>(lua_touserdata(L_, i.raw()));
}

namespace detail {

template <auto MemFn>
    requires member_function_pointer<decltype(MemFn)>
int invoke_method(lua_State* L) noexcept {
    using traits = member_function_traits<decltype(MemFn)>;
    using class_t = typename traits::class_type;
    using ret_t = typename traits::return_type;
    using args_t = typename traits::args_tuple;

    auto& self = check_userdata<class_t>(L, idx{1});
    auto args = extract_args<args_t>(L, 2);

    if constexpr (std::is_void_v<ret_t>) {
        std::apply([&self](auto&&... a) { (self.*MemFn)(a...); }, args);
        return 0;
    } else if constexpr (is_multi_return_v<ret_t>) {
        auto mr = std::apply([&self](auto&&... a) { return (self.*MemFn)(a...); }, args);
        constexpr auto n = std::tuple_size_v<decltype(mr.values)>;
        push_tuple(L, mr.values, std::make_index_sequence<n>{});
        return static_cast<int>(n);
    } else {
        auto result = std::apply([&self](auto&&... a) { return (self.*MemFn)(a...); }, args);
        stack_traits<ret_t>::push(L, result);
        return 1;
    }
}

} // namespace detail

template <auto MemFn>
    requires detail::member_function_pointer<decltype(MemFn)>
[[nodiscard]] consteval lua_CFunction wrap_method() noexcept {
    return &detail::invoke_method<MemFn>;
}

template <typename T>
class class_binder {
    lua_State* L_;
    // Stack layout: [..., metatable, constructor_table]
    // metatable_idx is absolute index of the metatable
    int metatable_idx_;

public:
    class_binder(lua_State* L, int mt_idx) noexcept : L_(L), metatable_idx_(mt_idx) {}

    template <typename... Args>
    class_binder& ctor(const char* name = "new") noexcept {
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto args = detail::extract_args<std::tuple<std::decay_t<Args>...>>(L, 1);
            void* mem = lua_newuserdata(L, sizeof(T));
            std::apply([mem](auto&&... a) {
                new (mem) T(std::forward<decltype(a)>(a)...);
            }, args);
            luaL_getmetatable(L, userdata_traits<T>::name);
            lua_setmetatable(L, -2);
            return 1;
        });
        // Set on constructor table (top of stack)
        lua_setfield(L_, -2, name);
        return *this;
    }

    template <auto MemFn>
    class_binder& method(const char* name) noexcept {
        lua_pushcfunction(L_, &detail::invoke_method<MemFn>);
        // Set on metatable for instance method lookup
        lua_setfield(L_, metatable_idx_, name);
        return *this;
    }

    class_binder& destructor() noexcept {
        lua_pushcfunction(L_, [](lua_State* L) -> int {
            auto* obj = static_cast<T*>(lua_touserdata(L, 1));
            if (obj) obj->~T();
            return 0;
        });
        lua_setfield(L_, metatable_idx_, "__gc");
        return *this;
    }

    template <auto MemFn>
    class_binder& meta(const char* name) noexcept {
        lua_pushcfunction(L_, &detail::invoke_method<MemFn>);
        lua_setfield(L_, metatable_idx_, name);
        return *this;
    }

    // Finalize: set constructor table as global and pop metatable
    void finalize(const char* name) noexcept {
        // Stack: [..., metatable, constructor_table]
        lua_setfield(L_, LUA_GLOBALSINDEX, name); // pops constructor table
        lua_pop(L_, 1); // pop metatable
    }
};

template <typename T>
[[nodiscard]] class_binder<T> register_class(state_view L) noexcept {
    static_assert(userdata_traits<T>::name != nullptr,
                  "Specialize userdata_traits<T> with a name before registering");

    luaL_newmetatable(L.raw(), userdata_traits<T>::name);
    lua_pushvalue(L.raw(), -1);
    lua_setfield(L.raw(), -2, "__index");

    int mt_idx = lua_gettop(L.raw()); // absolute index of metatable

    lua_newtable(L.raw()); // constructor table

    return class_binder<T>{L.raw(), mt_idx};
}

inline void finalize_class(state_view L, const char* name) noexcept {
    // Stack: [..., metatable, constructor_table]
    lua_setfield(L.raw(), LUA_GLOBALSINDEX, name); // pops constructor table
    lua_pop(L.raw(), 1); // pop metatable
}

// ============================================================================
// Layer 6: table_ref — registry-backed table reference
// ============================================================================

class table_ref {
    lua_State* L_;
    int ref_;

public:
    table_ref(state_view L, idx i) noexcept : L_(L.raw()) {
        lua_pushvalue(L_, i.raw());
        ref_ = luaL_ref(L_, LUA_REGISTRYINDEX);
    }

    ~table_ref() {
        if (ref_ != LUA_NOREF)
            luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
    }

    table_ref(table_ref&& other) noexcept : L_(other.L_), ref_(other.ref_) {
        other.ref_ = LUA_NOREF;
    }

    table_ref& operator=(table_ref&& other) noexcept {
        if (this != &other) {
            if (ref_ != LUA_NOREF)
                luaL_unref(L_, LUA_REGISTRYINDEX, ref_);
            L_ = other.L_;
            ref_ = other.ref_;
            other.ref_ = LUA_NOREF;
        }
        return *this;
    }

    table_ref(const table_ref&) = delete;
    table_ref& operator=(const table_ref&) = delete;

    void push() const noexcept {
        lua_rawgeti(L_, LUA_REGISTRYINDEX, ref_);
    }

    template <typename T>
    [[nodiscard]] T get(const char* key) const noexcept {
        push();
        lua_getfield(L_, -1, key);
        T val = stack_traits<T>::get(L_, -1);
        lua_pop(L_, 2);
        return val;
    }

    template <typename T>
    void set(const char* key, T&& value) noexcept {
        push();
        stack_traits<std::decay_t<T>>::push(L_, std::forward<T>(value));
        lua_setfield(L_, -2, key);
        lua_pop(L_, 1);
    }

    template <typename T>
    [[nodiscard]] T raw_geti(int n) const noexcept {
        push();
        lua_rawgeti(L_, -1, n);
        T val = stack_traits<T>::get(L_, -1);
        lua_pop(L_, 2);
        return val;
    }

    template <typename T>
    void raw_seti(int n, T&& value) noexcept {
        push();
        stack_traits<std::decay_t<T>>::push(L_, std::forward<T>(value));
        lua_rawseti(L_, -2, n);
        lua_pop(L_, 1);
    }

    [[nodiscard]] std::size_t length() const noexcept {
        push();
        std::size_t len = lua_objlen(L_, -1);
        lua_pop(L_, 1);
        return len;
    }

    [[nodiscard]] bool valid() const noexcept {
        return ref_ != LUA_NOREF && ref_ != LUA_REFNIL;
    }
};

// ============================================================================
// Layer 7: Container views — table_array, table_map
// ============================================================================

// --- table_array: R/W random-access view over a Lua table's array portion ---

class table_array_element {
    lua_State* L_;
    int table_;
    int lua_i_;

public:
    constexpr table_array_element(lua_State* L, int table, int lua_i) noexcept
        : L_(L), table_(table), lua_i_(lua_i) {}

    template <typename T>
    [[nodiscard]] T as() const noexcept {
        lua_rawgeti(L_, table_, lua_i_);
        T val = stack_traits<T>::get(L_, -1);
        lua_pop(L_, 1);
        return val;
    }

    [[nodiscard]] luapp::type type() const noexcept {
        lua_rawgeti(L_, table_, lua_i_);
        auto t = static_cast<luapp::type>(lua_type(L_, -1));
        lua_pop(L_, 1);
        return t;
    }

    template <typename T>
    table_array_element& operator=(T&& val) noexcept {
        stack_traits<std::decay_t<T>>::push(L_, std::forward<T>(val));
        lua_rawseti(L_, table_, lua_i_);
        return *this;
    }

    [[nodiscard]] int index() const noexcept { return lua_i_; }
};

class table_array {
    lua_State* L_;
    int table_;

public:
    table_array(lua_State* L, abs_idx table_i) noexcept
        : L_(L), table_(table_i.raw()) {}

    [[nodiscard]] std::size_t size() const noexcept {
        return lua_objlen(L_, table_);
    }

    [[nodiscard]] table_array_element operator[](int i) const noexcept {
        return {L_, table_, i};
    }

    class iterator {
        lua_State* L_;
        int table_;
        int pos_;

    public:
        using iterator_category = std::random_access_iterator_tag;
        using value_type        = table_array_element;
        using difference_type   = int;
        using pointer           = void;
        using reference         = table_array_element;

        constexpr iterator(lua_State* L, int table, int pos) noexcept
            : L_(L), table_(table), pos_(pos) {}

        [[nodiscard]] table_array_element operator*() const noexcept { return {L_, table_, pos_}; }
        [[nodiscard]] table_array_element operator[](int n) const noexcept { return {L_, table_, pos_ + n}; }

        iterator& operator++() noexcept { ++pos_; return *this; }
        iterator  operator++(int) noexcept { auto tmp = *this; ++pos_; return tmp; }
        iterator& operator--() noexcept { --pos_; return *this; }
        iterator  operator--(int) noexcept { auto tmp = *this; --pos_; return tmp; }

        iterator& operator+=(int n) noexcept { pos_ += n; return *this; }
        iterator& operator-=(int n) noexcept { pos_ -= n; return *this; }

        friend iterator operator+(iterator it, int n) noexcept { return {it.L_, it.table_, it.pos_ + n}; }
        friend iterator operator+(int n, iterator it) noexcept { return {it.L_, it.table_, it.pos_ + n}; }
        friend iterator operator-(iterator it, int n) noexcept { return {it.L_, it.table_, it.pos_ - n}; }
        friend int operator-(iterator a, iterator b) noexcept { return a.pos_ - b.pos_; }

        friend bool operator==(iterator a, iterator b) noexcept { return a.pos_ == b.pos_; }
        friend auto operator<=>(iterator a, iterator b) noexcept { return a.pos_ <=> b.pos_; }
    };

    [[nodiscard]] iterator begin() const noexcept { return {L_, table_, 1}; }
    [[nodiscard]] iterator end() const noexcept {
        return {L_, table_, static_cast<int>(lua_objlen(L_, table_)) + 1};
    }
};

// --- table_map: forward-iterable view over all key-value pairs ---

struct table_map_kv {
    idx key;
    idx value;
};

struct table_map_sentinel {};

class table_map_iterator {
    lua_State* L_;
    int table_;
    int saved_top_;
    bool active_;

    void advance() noexcept {
        if (lua_next(L_, table_) != 0) {
            active_ = true;
        } else {
            active_ = false;
        }
    }

public:
    using iterator_category = std::input_iterator_tag;
    using value_type        = table_map_kv;
    using difference_type   = std::ptrdiff_t;
    using pointer           = void;
    using reference         = table_map_kv;

    table_map_iterator(lua_State* L, int table) noexcept
        : L_(L), table_(table), saved_top_(lua_gettop(L)), active_(false) {
        lua_pushnil(L_);
        advance();
    }

    ~table_map_iterator() noexcept {
        if (active_ && L_)
            lua_settop(L_, saved_top_);
    }

    table_map_iterator(table_map_iterator&& other) noexcept
        : L_(other.L_), table_(other.table_),
          saved_top_(other.saved_top_), active_(other.active_) {
        other.L_ = nullptr;
        other.active_ = false;
    }

    table_map_iterator& operator=(table_map_iterator&& other) noexcept {
        if (this != &other) {
            if (active_ && L_) lua_settop(L_, saved_top_);
            L_ = other.L_;
            table_ = other.table_;
            saved_top_ = other.saved_top_;
            active_ = other.active_;
            other.L_ = nullptr;
            other.active_ = false;
        }
        return *this;
    }

    table_map_iterator(const table_map_iterator&) = delete;
    table_map_iterator& operator=(const table_map_iterator&) = delete;

    [[nodiscard]] table_map_kv operator*() const noexcept {
        // key is at saved_top_+1, value at saved_top_+2 (lua_next pushes both)
        return {idx{saved_top_ + 1}, idx{saved_top_ + 2}};
    }

    table_map_iterator& operator++() noexcept {
        lua_pop(L_, 1); // pop value, keep key
        advance();
        return *this;
    }

    friend bool operator==(const table_map_iterator& it, table_map_sentinel) noexcept {
        return !it.active_;
    }

    friend bool operator==(table_map_sentinel s, const table_map_iterator& it) noexcept {
        return it == s;
    }
};

class table_map {
    lua_State* L_;
    int table_;

public:
    table_map(lua_State* L, abs_idx table_i) noexcept
        : L_(L), table_(table_i.raw()) {}

    [[nodiscard]] table_map_iterator begin() const noexcept {
        return table_map_iterator{L_, table_};
    }

    [[nodiscard]] table_map_sentinel end() const noexcept { return {}; }
};

// --- Out-of-line factory method definitions ---

inline table_array state_view::arr(idx table_i) noexcept {
    return table_array{L_, to_abs(table_i)};
}

inline table_array state_view::arr(abs_idx table_i) noexcept {
    return table_array{L_, table_i};
}

inline table_map state_view::map(idx table_i) noexcept {
    return table_map{L_, to_abs(table_i)};
}

inline table_map state_view::map(abs_idx table_i) noexcept {
    return table_map{L_, table_i};
}

} // namespace luapp

#endif // LUAPP_HPP_INCLUDED
