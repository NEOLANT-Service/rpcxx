// This file is a part of RPCXX project

/*
Copyright 2024 "NEOLANT Service", "NEOLANT Kalinigrad", Alexey Doronin, Anastasia Lugovets, Dmitriy Dyakonov

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

inline const char* codegen_script = R"(local make_type
local check_ns

args = args or {}

local function make_value(t, ...)
    if #{...} == 0 then return t end
    if t.__value__ then
        error(tostring(t)..'is already a value!')
    end
    if not t.__check__ then
        error("Type '"..tostring(t).."' does not support default values")
    end
    local res = make_type(nil, t)
    res.__subtype__ = "default"
    res.__value__ = t:__check__(...)
    return res
end

local function make_attrs(t, ...)
    local attrs = {...}
    for i, v in ipairs(attrs) do
        if type(v) ~= "string" and type(v) ~= "table" then
            error(":attrs(): Attribute #"..i.." is not a string or a table!")
        end
    end
    if #attrs == 0 then return t end
    local parent = t
    if t.__subtype__ == "attrs" then
        parent = t.__next__
    end
    local res = make_type(nil, t)
    res.__subtype__ = "attrs"
    res.__attrs__ = attrs
    return res
end

local function make_pack(t)
    if t.__subtype__ ~= "struct" then
        error(tostring(t).." is not a Struct. Pack (#) operation is not allowed")
    end
    if t.__is_pack__ then
        error(tostring(t).." is already a Pack. Pack (#) operation is not allowed")
    end
    local copy = {}
    for k, v in pairs(t) do
        copy[k] = v
    end
    copy.__is_pack__ = true
    return copy
end

local function make_signature(sig, ret)
    if sig.__return__ then
        error(sig.__name__.." already has a return type: "..
            sig.__return__.__name__.." cannot apply: "..ret.__name__)
    end
    sig.__return__ = ret
    return sig
end

local strict_mode = 0

local builtins = setmetatable({}, {
    __index = function(t, k)
        return strict_mode > 0 and error("Could not find: "..k) or rawget(t, k)
    end
})

local function new_state()
    return {
        types = setmetatable({}, {__index = builtins}),
        methods = {},
        notify = {},
        attrs = {},
        depth = 0,
        source = __current_file__ or "root",
        ns = nil,
    }
end

local state = new_state()
local namespaces = {}

setmetatable(_G, {
    __newindex = function (t, k, v)
        if type(v) == "table" and v.__is_type__ then
            if v.__subtype__ == "builtin" then
                v.__name__ = k
                builtins[k] = v
                return
            end
            v:__resolve_name__({k})
            if state.types[v.__name__] then
                error("Cannot redeclare type: "..tostring(v))
            end
            state.types[v.__name__] = v
        else
            rawset(t, k, v)
        end
    end,
    __index = function (t, k)
        return state.types[k]
    end
})

local function print_type(t)
    return "'"..tostring(t.__subtype__).." "..tostring(t.__ns__ or '').."."..tostring(t.__name__).."'"
end

local _defaultedCounter = 0
local function defaultedCount()
    _defaultedCounter = _defaultedCounter + 1
    return tostring(_defaultedCounter)
end

local function resolve_name(t, path)
    check_ns()
    path = path or {}
    if t.__ns__ == nil then
        t.__ns__ = state.ns
    end
    local sub = t.__subtype__
    if sub == "builtin" then
        return
    end
    if not t.__name__ then
        t.__name__ = table.concat(path, "__")
    end
    if sub == "struct" then
        for k, v in pairs(t.__fields__) do
            resolve_name(v, {table.unpack(path), k}, t)
        end
    elseif sub == "enum" then
        return
    else
        assert(t.__next__, "Missing wrapped type in sub: "..sub)
        resolve_name(t.__next__, path)
    end
end

make_type = function(name, _next, check_val)
    local t = setmetatable({
        __name__ = name,
        __is_type__ = true,
        __next__ = _next,
        __resolve_name__ = resolve_name,
        __source__ = state.source,
        __ns__ = state.ns,
        __ns_depth__ = state.depth,
        attrs = make_attrs,
    }, {
        __tostring = print_type,
        __call = make_value,
        __len = make_pack,
        __shr = make_signature,
    })
    if check_val then
        t.__check__ = check_val
    elseif _next then
        t.__check__ = _next.__check__
    end
    return t
end

local function resolve_type(v, name)
    if type(v) == "table" then
        if v.__is_type__ then
            return v
        else
            return struct()(v)
        end
    else
        error("Field '"..name.."' => type expected, but got: "..tostring(v))
    end
end

local function make_builtin(check_val)
    local result = make_type(nil, nil, check_val)
    result.__subtype__ = "builtin";
    return result
end

local function init_num(min, max)
    return function (t, ...)
        assert(#{...} == 1, "Number expects only one param")
        local res = ...
        assert(type(res) == "number", "Expected a number as default param")
        assert(res >= min and res <= max,
            res.." => does not fit into ["..min.." - "..max.."] for type: "..tostring(t))
        return res
    end
end

local function typed(name)
    return function (t, ...)
        assert(#{...} == 1, tostring(t)..": expects only one param")
        assert(type(...) == name, tostring(t)..": ".."Expected '"..name.."', got: "..tostring(...))
        return ...
    end
end

local populate = function ()
    local function integer(signed, bits)
        if signed then
            return init_num(-(2^(bits-1)), 2^(bits-1)-1)
        else
            return init_num(0, 2^bits-1)
        end
    end
    local s = {}
    local u = {}
    for _, v in ipairs{8, 16, 32, 64} do
        s[v] = integer(true, v)
        u[v] = integer(false, v)
    end
    return s, u
end
local signed, unsigned = populate()
local function is_priv(key)
    return key:sub(1, 2) == "__"
end

local function parse_signature(name, sig)
    assert(type(sig) == "table", "Signatures should be an array/table of types")
    local isNamed = false
    local isPack = false
    for k, v in pairs(sig) do
        if type(k) == "number" and isNamed then
            error(name..": Invalid signature, mixing named and positionals not allowed")
        end
        if type(k) == "string" then
            if is_priv(k) then
                goto continue
            end
            isNamed = true
        end
        assert(type(v) == "table", "Signature contains non type")
        assert(v.__is_type__, "Signature contains non type")
        if v.__is_pack__ then
            assert(#sig == 1, "#Pack allowed as the only type in signature")
            assert(not isNamed, "#Pack is not allowed to have named params")
            isPack = true
        end
        v:__resolve_name__({name, "arg", tostring(k)})
        ::continue::
    end
    if sig.__return__ then
        sig.__return__:__resolve_name__({name, "ret"})
    end
    return {
        named = isNamed,
        async = sig.__async__ or false,
        params = sig,
        pack = isPack,
        returns = sig.__return__ or void
    }
end

local function resolve_inc(was, path)
    if __resolve_inc__ then
        return __resolve_inc__(was, path)
    else
        return path
    end
end

local function enter_file(file)
    local was = state
    state = new_state()
    state.source = resolve_inc(was.source, file)
    state.depth = was.depth + 1
    state.ns = nil
    return was
end

local function exit_into(was)
    check_ns()
    namespaces[state.ns] = state
    local catched = state.types
    state = namespaces[was.ns]
    return catched
end

local function exit_strict()
    if strict_mode > 0 then
        strict_mode = strict_mode - 1
    end
end

local function in_strict(func)
    strict_mode = strict_mode + 1
    return function(...)
        local res = func(...)
        exit_strict()
        return res
    end
end

check_ns = function()
    assert(state.ns ~= nil, "use 'namespace <ns>' directive first")
end

-- User facing definitions

void = make_builtin()
noreturn = make_builtin()
nothing = make_builtin()

binary = make_builtin()
binary_view = make_builtin()

int8 = make_builtin(signed[8])
i8 = make_builtin(signed[8])

uint8 = make_builtin(unsigned[8])
u8 = make_builtin(unsigned[8])

int16 = make_builtin(signed[16])
i16 = make_builtin(signed[16])

uint16 = make_builtin(unsigned[16])
u16 = make_builtin(unsigned[16])

json = make_builtin()
json_view = make_builtin()

boolean = make_builtin(typed("boolean"))
bool = make_builtin(typed("boolean"))

int = make_builtin(signed[32])
int32 = make_builtin(signed[32])
i32 = make_builtin(signed[32])

uint32 = make_builtin(unsigned[32])
u32 = make_builtin(unsigned[32])
uint = make_builtin(unsigned[32])

int64 = make_builtin(signed[64])
i64 = make_builtin(signed[64])

uint64 = make_builtin(unsigned[64])
u64 = make_builtin(unsigned[64])

f32 = make_builtin(typed("number"))
float = make_builtin(typed("number"))

double = make_builtin(typed("number"))
number = make_builtin(typed("number"))
f64 = make_builtin(typed("number"))

str = make_builtin(typed("string"))
string = make_builtin(typed("string"))
string.__name__ = "string"
string_view = make_builtin(typed("string"))

local function check_ident(id)
    return id --TODO: check that id is valid alphanum identifier
end

local function init_opts(opts)
    if type(opts) == "string" then
        return {name = opts}
    else
        return opts or {}
    end
end

local function _check_struct(table)
    if type(table) == "table" and not table.__is_type__ then
        return struct()(table)
    else
        return table
    end
end

-- User facing functions

function methods(opts)
    opts = init_opts(opts)
    return in_strict(function(values)
        for method, sig in pairs(values) do
            assert(type(method) == "string")
            local parsed = parse_signature(method, sig)
            parsed.service = opts.name or "RPC"
            parsed.name = check_ident(method)
            table.insert(state.methods, parsed)
        end
    end)
end

function notify(opts)
    opts = init_opts(opts)
    return in_strict(function(values)
        for notif, sig in pairs(values) do
            assert(type(notif) == "string")
            local parsed = parse_signature(notif, sig)
            assert(parsed.returns.__name__ == "void", notif..": Notifications cannot have return types")
            parsed.service = opts.name or "RPC"
            parsed.name = check_ident(notif)
            table.insert(state.notify, parsed)
        end
    end)
end

function struct(opts)
    check_ns()
    opts = init_opts(opts)
    return in_strict(function(shape)
        local check = function(s, shape)
            assert(type(shape) == "table", "struct default value should be table")
            local res = {}
            for k, v in pairs(s.__fields__) do
                if shape[k] then
                    if not v.__check__ then
                        error("Field of struct: "..k.." ("..tostring(v)..") does not support default values")
                    end
                    res[k] = v:__check__(shape[k])
                end
            end
            return res
        end
        local res = make_type(opts.name, nil, check)
        assert(type(shape) == "table", '{field = t, ...} expected for struct()')
        res.__subtype__ = "struct"
        res.__fields__ = {}
        for k, v in pairs(shape) do
            if not is_priv(k) then
                res.__fields__[k] = resolve_type(v, k)
            end
        end
        return res
    end)
end

function enum(opts)
    check_ns()
    opts = init_opts(opts)
    return in_strict(function(values)
        assert(type(values) == "table", '{name, name2, ...} expected for enum()')
        local check = function(s, v)
            assert(type(v) == "string", "enum default value should be a string")
            for i, f in ipairs(s.__fields__) do
                if f == v then
                    return v
                end
            end
            error(v.." is not a valid member of "..tostring(s))
        end
        local res = make_type(opts.name, nil, check)
        res.__subtype__ = "enum"
        res.__fields__ = values
        for k, v in pairs(values) do
            if type(k) == "number" then
                assert(type(k) == "number", "Enum keys should be integers")
                assert(type(v) == "string", "Enum values should be strings")
            else
                assert(type(k) == "string", "Strict Enum keys should be strings")
                assert(type(v) == "number", "Strict Enum values should be integers")
            end
            res[k] = v
        end
        return res
    end)
end

function routes(opts)
    check_ns()
    opts = init_opts(opts)
    return in_strict(function(...)
        -- annotation only
    end)
end

function async(signature)
    assert(type(signature) == "table", "async can be used only with signatures: {t, t}")
    signature.__async__ = true
    return signature
end

function include(path)
    local was = enter_file(path)
    dofile(state.source)
    return exit_into(was)
end

function Optional(_next)
    assert(_next, 'Could not find type for Optional()')
    _next = _check_struct(_next)
    local res = make_type(nil, _next)
    res.__subtype__ = "opt"
    return res
end

function Array(_next)
    assert(_next, 'Could not find type for Array()')
    _next = _check_struct(_next)
    local check = function(t, arr)
        local res = {}
        for i, v in pairs(arr) do
            assert(type(i) == "number", "default value for Array() must be an array")
            res[i] = _next:__check__(v)
        end
        return res
    end
    local res = make_type(nil, _next, check)
    res.__subtype__ = "arr"
    return res
end

function Map(_next)
    assert(_next, 'Could not find type for Map()')
    _next = _check_struct(_next)
    local check = function(t, obj)
        local res = {}
        for k, v in pairs(obj) do
            assert(type(k) == "string", "default value for Map() must be a table")
            res[k] = _next:__check__(v)
        end
        return res
    end
    local res = make_type(nil, _next, check)
    res.__subtype__ = "map"
    return res
end

function Alias(_next)
    assert(_next, 'Could not find type for Alias()')
    _next = _check_struct(_next)
    local res = make_type(nil, _next)
    res.__subtype__ = "alias"
    return res
end

function syntax(s)
    assert(s == "rpcxx", "Unsupported syntax: "..s)
end

function namespace(ns)
    assert(ns ~= "__root__", "name '__root__' is reserved")
    assert(type(ns) == "string", "namespace should be a string")
    assert(state.ns == nil, "namespace was already set to: "..tostring(state.ns))
    state.ns = ns
    namespaces[state.ns] = state
    for _, v in pairs(state.types) do
        v.__ns__ = ns
    end
    if namespaces.__root__ == nil then
        namespaces.__root__ = state
    end
end

return namespaces



)";
