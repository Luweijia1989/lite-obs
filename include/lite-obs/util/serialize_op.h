#pragma once

#include <cstddef>
#include <vector>

class serialize_op
{
public:
    serialize_op(std::vector<uint8_t> &d) : data(d) {
    }
    inline void s_w8(uint8_t u8)
    {
        s_write(&u8, sizeof(uint8_t));
    }

    inline void s_wl16(uint16_t u16)
    {
        s_w8((uint8_t)u16);
        s_w8(u16 >> 8);
    }

    inline void s_wl24(uint32_t u24)
    {
        s_w8((uint8_t)u24);
        s_wl16((uint16_t)(u24 >> 8));
    }

    inline void s_wl32(uint32_t u32)
    {
        s_wl16((uint16_t)u32);
        s_wl16((uint16_t)(u32 >> 16));
    }

    inline void s_wl64(uint64_t u64)
    {
        s_wl32((uint32_t)u64);
        s_wl32((uint32_t)(u64 >> 32));
    }

    inline void s_wlf(float f)
    {
        s_wl32(*(uint32_t *)&f);
    }

    inline void s_wld(double d)
    {
        s_wl64(*(uint64_t *)&d);
    }

    inline void s_wb16(uint16_t u16)
    {
        s_w8(u16 >> 8);
        s_w8((uint8_t)u16);
    }

    inline void s_wb24(uint32_t u24)
    {
        s_wb16((uint16_t)(u24 >> 8));
        s_w8((uint8_t)u24);
    }

    inline void s_wb32(uint32_t u32)
    {
        s_wb16((uint16_t)(u32 >> 16));
        s_wb16((uint16_t)u32);
    }

    inline void s_wb64(uint64_t u64)
    {
        s_wb32((uint32_t)(u64 >> 32));
        s_wb32((uint32_t)u64);
    }

    inline void s_wbf(float f)
    {
        s_wb32(*(uint32_t *)&f);
    }

    inline void s_wbd(double d)
    {
        s_wb64(*(uint64_t *)&d);
    }

    inline void s_write(const void *d, size_t size)
    {
        auto old_num = data.size();
        data.resize(data.size() + size);
        memcpy(data.data() + old_num, d, size);
    }
private:
    std::vector<uint8_t> &data;
};
