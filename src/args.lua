local ffi = require 'ffi'
local uint8_t = ffi.typeof 'uint8_t[?]'
local argsBuffer
local processName = 'luac.ps-exe'

function createArgsBuffer(...)
    local args = { ... }
    local spaceNeeded = #processName + 2
    for i, v in ipairs(args) do
        local a = tostring(v)
        spaceNeeded = spaceNeeded + #a + 1
    end
    argsBuffer = ffi.new(uint8_t, spaceNeeded)
    local ptr = ffi.cast('uint8_t *', argsBuffer)
    ffi.copy(ptr, processName)
    ptr = ptr + #processName + 1
    for i, v in ipairs(args) do
        local a = tostring(v)
        ffi.copy(ptr, a)
        ptr = ptr + #a + 1
    end
    ptr[0] = 0
end

function UnknownMemoryRead(address, size)
    if size ~= 1 then return 0xffffffff end
    local offset = address - 0x40000000
    if offset < 0 then return 0xff end
    if offset >= ffi.sizeof(argsBuffer) then return 0xff end
    return argsBuffer[offset]
end

createArgsBuffer()
