# This file is a part of RPCXX project

"""
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
"""

from dumper import Children, SubItem, UnnamedSubItem, DumperBase
from utils import DisplayFormat, TypeCode

def _printNull(d, value):
    d.putValue("<null>")

def _printBool(d, value):
    num = value["d"]["boolean"].integer()
    if num == 1: d.putValue("true")
    elif num == 0: d.putValue("false")
    else: d.putValue("<invalid bool: %i>" % num)

def _printFloat(d, value):
    try:
        d.putValue(value["d"]["number"].floatingPoint())
    except Exception as e:
        d.putValue("<number err: %s>" % str(e))

def _printInt(d, value):
    try:
        d.putValue(value["d"]["integer"].integer())
    except Exception as e:
        d.putValue("<int err: %s>" % str(e))

def _printUint(d, value):
    try:
        d.putValue(value["d"]["uinteger"].integer())
    except Exception as e:
        d.putValue("<uint err: %s>" % str(e))

def _printBin(d, value):
    size = value["size"].integer()
    if not size:
        d.putValue("<binary[0]>")
    else:
        d.putArrayData(value["d"]["binary"].pointer(), size, d.lookupType("uint8_t").stripTypedefs())

def _printStr(d, value):
    size = value["size"].integer()
    if not size:
        d.putValue("<string[0]>")
    d.putCharArrayHelper(value["d"]["string"].pointer(), size, d.createType("char"), d.currentItemFormat())

def _printDiscard(d, value):
    d.putValue("<discarded>")
    d.putExpandable()
    if d.isExpanded():
        with Children(d):
            with SubItem(d, "<reason>"):
                _printStr(d, value)

def attempt(d, func, name):
    try:
        return func()
    except Exception as e:
        d.putValue("%s: %s" % (name, str(e)))

def _printArray(d, value):
    size = attempt(d, lambda: value["size"].integer(), "arr size")
    ptr = attempt(d, lambda: value["d"]["array"].pointer(), "arr ptr")
    if size is None: return
    if ptr is None: return
    d.putValue("<array[%i]>" % size)
    d.putExpandable()
    if d.isExpanded():
        try:
            d.putArrayData(ptr, size, d.createType("jv::JsonView"))
        except Exception as e: 
            d.putValue("<array[%i@0x%x]: %s>" % (size, ptr, e))

def _printObject(d, value):
    size = attempt(d, lambda: value["size"].integer(), "obj size")
    ptr = attempt(d, lambda: value["d"]["object"].pointer(), "obj ptr")
    if size is None: return
    if ptr is None: return
    d.putValue("<object[%i]>" % size)
    d.putExpandable()
    if d.isExpanded():
        try:
            d.putArrayData(ptr, size, d.lookupType("jv::JsonPair"))
        except Exception as e:
            d.putValue("<object[%i@0x%x]: %s>" % (size, ptr, e))

def _printInvalid(d, value):
    d.putValue("<invalid>")

def _printCustom(d, value):
    d.putValue("<custom> ptr(%i) size(%i)" % value["d"]["custom"].pointer() % value["size"].integer())

_jsonPrinters = {
    0: _printNull,
    1: _printBin,
    2: _printBool,
    4: _printFloat,
    8: _printStr,
    16: _printInt,
    32: _printUint,
    64: _printArray,
    128: _printObject,
    256: _printDiscard,
    512: _printCustom,
}

def qdump__jv__JsonView(d, value):
    printer = _jsonPrinters.get(value["data"]["type"].integer(), _printInvalid)
    try:
        printer(d, value["data"])
    except Exception as e:
        d.putValue("<%s => %s>" % (printer.__name__, str(e)))

def qdump__jv__Json(d, value):
   qdump__jv__JsonView(d, value["view"])

_mutPrinters = {
    0: lambda d, v: d.putValue("<null>"),
    1: lambda d, v: d.putItem(v["bin"].dereference()),
    2: lambda d, v: d.putItem(v["boolean"]),
    4: lambda d, v: d.putItem(v["number"]),
    8: lambda d, v: d.putItem(v["str"].dereference()),
    16: lambda d, v: d.putValue(v["integer"].integer()),
    32: lambda d, v: d.putValue(v["uinteger"].integer()),
    64: lambda d, v: d.putItem(v["arr"].dereference()),
    128: lambda d, v: d.putItem(v["obj"].dereference()),
}

def qdump__jv__BasicMutJson(d, value):
    t = value["data"]["type"].integer()
    _mutPrinters.get(t, _printInvalid)(d, value["data"])

def qdump__jv__JsonPointer(d, value):
   size = value["size"].integer()
   ptr = value["keys"].pointer()
   d.putValue("JsonPointer[%i]" % size)
   d.putExpandable()
   if d.isExpanded():
        d.putArrayData(ptr, size, d.lookupType("jv::JsonKey"))

def qdump__rpcxx__FastPimpl(d, value):
    t = value.type[0]
    addr = value["buff"].address()
    d.putAddress(addr)
    d.putValue('@0x%x' % addr)
    d.putExpandable()
    if d.isExpanded():
        with Children(d):
            d.putFields(d.createValue(addr, t))

# TODO:
# def qdump__rc__Weak(d, value):
#     t = value.type[0]
#     data = value["block"]["data"]
#     if not data.address(): 
#         d.putValue("<null>")
#     else:
#         d.putTypedPointer(data["data"].address(), t)

def qdump__std__basic_string_view(d, value):
    ch_t = value.type[0]
    if d.isMsvcTarget():
        data = value["_Mydata"]
        size = value["_Mysize"]
    else:
        data = value["_M_str"]
        size = value["_M_len"]
    d.putCharArrayHelper(
        data.pointer(), size.integer(), ch_t, d.currentItemFormat()
    )

from stdtypes import qdumpHelper_std__string

def qdump__std__filesystem__path(d, value):
    str = value["_M_pathname"]
    if d.isMsvcTarget():
        ch = d.createType("wchar_t")
    else:
        ch = d.createType("char")
    qdumpHelper_std__string(d, str, ch, d.currentItemFormat())

def qdump__std__filesystem____cxx11__path(d, value):
    qdump__std__filesystem__path(d, value)
