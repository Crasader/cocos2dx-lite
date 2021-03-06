/****************************************************************************
Copyright (c) 2010-2012 cocos2d-x.org
Copyright (c) 2013-2017 Chukong Technologies Inc.

http://www.cocos2d-x.org

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.
****************************************************************************/


#include "platform/CCImage.h"

#include <string>
#include <ctype.h>

#include "base/CCData.h"
#include "base/ccConfig.h" // CC_USE_JPEG, CC_USE_TIFF, CC_USE_WEBP

extern "C"
{
    // To resolve link error when building 32bits with Xcode 6.
    // More information please refer to the discussion in https://github.com/cocos2d/cocos2d-x/pull/6986
#if defined (__unix) || (CC_TARGET_PLATFORM == CC_PLATFORM_IOS)
#ifndef __ENABLE_COMPATIBILITY_WITH_UNIX_2003__
#define __ENABLE_COMPATIBILITY_WITH_UNIX_2003__
#include <stdio.h>
#include <dirent.h>
    FILE *fopen$UNIX2003( const char *filename, const char *mode )
    {
        return fopen(filename, mode);
    }
    size_t fwrite$UNIX2003( const void *a, size_t b, size_t c, FILE *d )
    {
        return fwrite(a, b, c, d);
    }
    int fputs$UNIX2003(const char *res1, FILE *res2){
        return fputs(res1,res2);
    }
    char *strerror$UNIX2003( int errnum )
    {
        return strerror(errnum);
    }
    DIR * opendir$INODE64$UNIX2003( char * dirName )
    {
        return opendir( dirName );
    }
    DIR * opendir$INODE64( char * dirName )
    {
        return opendir( dirName );
    }

    int closedir$UNIX2003(DIR * dir)
    {
        return closedir(dir);
    }

    struct dirent * readdir$INODE64( DIR * dir )
    {
        return readdir( dir );
    }
#endif
#endif

#if CC_USE_PNG
#include "png.h"
#endif //CC_USE_PNG

#if CC_USE_TIFF
#include "tiffio.h"
#endif //CC_USE_TIFF

#include "base/etc1.h"
    
#if CC_USE_JPEG
#include "jpeglib.h"
#endif // CC_USE_JPEG
}
#include "base/s3tc.h"
#include "base/atitc.h"
#include "base/pvr.h"
#include "base/TGAlib.h"

#if CC_USE_WEBP
#include "decode.h"
#endif // CC_USE_WEBP

#include "base/ccMacros.h"
#include "platform/CCCommon.h"
#include "platform/CCStdC.h"
#include "platform/CCFileUtils.h"
#include "base/CCConfiguration.h"
#include "base/ccUtils.h"
#include "base/ZipUtils.h"
#if (CC_TARGET_PLATFORM == CC_PLATFORM_ANDROID)
#include "platform/android/CCFileUtils-android.h"
#endif

#define CC_GL_ATC_RGB_AMD                                          0x8C92
#define CC_GL_ATC_RGBA_EXPLICIT_ALPHA_AMD                          0x8C93
#define CC_GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD                      0x87EE

NS_CC_BEGIN

//////////////////////////////////////////////////////////////////////////
//struct and data for pvr structure

namespace
{
    static const int PVR_TEXTURE_FLAG_TYPE_MASK = 0xff;
    
    static bool _PVRHaveAlphaPremultiplied = false;
    
    // Values taken from PVRTexture.h from http://www.imgtec.com
    enum class PVR2TextureFlag
    {
        Mipmap         = (1<<8),        // has mip map levels
        Twiddle        = (1<<9),        // is twiddled
        Bumpmap        = (1<<10),       // has normals encoded for a bump map
        Tiling         = (1<<11),       // is bordered for tiled pvr
        Cubemap        = (1<<12),       // is a cubemap/skybox
        FalseMipCol    = (1<<13),       // are there false colored MIP levels
        Volume         = (1<<14),       // is this a volume texture
        Alpha          = (1<<15),       // v2.1 is there transparency info in the texture
        VerticalFlip   = (1<<16),       // v2.1 is the texture vertically flipped
    };
    
    enum class PVR3TextureFlag
    {
        PremultipliedAlpha  = (1<<1)    // has premultiplied alpha
    };
    
    static const char gPVRTexIdentifier[5] = "PVR!";
    
    // v2
    enum class PVR2TexturePixelFormat : unsigned char
    {
        RGBA4444 = 0x10,
        RGBA5551,
        RGBA8888,
        RGB565,
        RGB555,          // unsupported
        RGB888,
        I8,
        AI88,
        PVRTC2BPP_RGBA,
        PVRTC4BPP_RGBA,
        BGRA8888,
        A8,
    };
        
    // v3
    enum class PVR3TexturePixelFormat : uint64_t
    {
        PVRTC2BPP_RGB  = 0ULL,
        PVRTC2BPP_RGBA = 1ULL,
        PVRTC4BPP_RGB  = 2ULL,
        PVRTC4BPP_RGBA = 3ULL,
        PVRTC2_2BPP_RGBA = 4ULL,
        PVRTC2_4BPP_RGBA  = 5ULL,
        ETC1 = 6ULL,
        DXT1 = 7ULL,
        DXT2 = 8ULL,
        DXT3 = 9ULL,
        DXT4 = 10ULL,
        DXT5 = 11ULL,
        BC1 = 7ULL,
        BC2 = 9ULL,
        BC3 = 11ULL,
        BC4 = 12ULL,
        BC5 = 13ULL,
        BC6 = 14ULL,
        BC7 = 15ULL,
        UYVY = 16ULL,
        YUY2 = 17ULL,
        BW1bpp = 18ULL,
        R9G9B9E5 = 19ULL,
        RGBG8888 = 20ULL,
        GRGB8888 = 21ULL,
        ETC2_RGB = 22ULL,
        ETC2_RGBA = 23ULL,
        ETC2_RGBA1 = 24ULL,
        EAC_R11_Unsigned = 25ULL,
        EAC_R11_Signed = 26ULL,
        EAC_RG11_Unsigned = 27ULL,
        EAC_RG11_Signed = 28ULL,
            
        BGRA8888       = 0x0808080861726762ULL,
        RGBA8888       = 0x0808080861626772ULL,
        RGBA4444       = 0x0404040461626772ULL,
        RGBA5551       = 0x0105050561626772ULL,
        RGB565         = 0x0005060500626772ULL,
        RGB888         = 0x0008080800626772ULL,
        A8             = 0x0000000800000061ULL,
        L8             = 0x000000080000006cULL,
        LA88           = 0x000008080000616cULL,
    };
        
        
    // v2
    typedef const std::map<PVR2TexturePixelFormat, Texture2D::PixelFormat> _pixel2_formathash;
    
    static const _pixel2_formathash::value_type v2_pixel_formathash_value[] =
    {
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::BGRA8888,        Texture2D::PixelFormat::BGRA8888),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA8888,        Texture2D::PixelFormat::RGBA8888),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA4444,        Texture2D::PixelFormat::RGBA4444),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGBA5551,        Texture2D::PixelFormat::RGB5A1),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGB565,      Texture2D::PixelFormat::RGB565),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::RGB888,      Texture2D::PixelFormat::RGB888),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::A8,          Texture2D::PixelFormat::A8),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::I8,          Texture2D::PixelFormat::I8),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::AI88,            Texture2D::PixelFormat::AI88),
            
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::PVRTC2BPP_RGBA,      Texture2D::PixelFormat::PVRTC2A),
        _pixel2_formathash::value_type(PVR2TexturePixelFormat::PVRTC4BPP_RGBA,      Texture2D::PixelFormat::PVRTC4A),
    };
        
    static const int PVR2_MAX_TABLE_ELEMENTS = sizeof(v2_pixel_formathash_value) / sizeof(v2_pixel_formathash_value[0]);
    static const _pixel2_formathash v2_pixel_formathash(v2_pixel_formathash_value, v2_pixel_formathash_value + PVR2_MAX_TABLE_ELEMENTS);
        
    // v3
    typedef const std::map<PVR3TexturePixelFormat, Texture2D::PixelFormat> _pixel3_formathash;
    static _pixel3_formathash::value_type v3_pixel_formathash_value[] =
    {
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::BGRA8888,    Texture2D::PixelFormat::BGRA8888),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA8888,    Texture2D::PixelFormat::RGBA8888),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA4444,    Texture2D::PixelFormat::RGBA4444),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGBA5551,    Texture2D::PixelFormat::RGB5A1),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGB565,      Texture2D::PixelFormat::RGB565),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::RGB888,      Texture2D::PixelFormat::RGB888),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::A8,          Texture2D::PixelFormat::A8),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::L8,          Texture2D::PixelFormat::I8),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::LA88,        Texture2D::PixelFormat::AI88),
            
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC2BPP_RGB,       Texture2D::PixelFormat::PVRTC2),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC2BPP_RGBA,      Texture2D::PixelFormat::PVRTC2A),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC4BPP_RGB,       Texture2D::PixelFormat::PVRTC4),
        _pixel3_formathash::value_type(PVR3TexturePixelFormat::PVRTC4BPP_RGBA,      Texture2D::PixelFormat::PVRTC4A),

        _pixel3_formathash::value_type(PVR3TexturePixelFormat::ETC1,        Texture2D::PixelFormat::ETC),
    };
        
    static const int PVR3_MAX_TABLE_ELEMENTS = sizeof(v3_pixel_formathash_value) / sizeof(v3_pixel_formathash_value[0]);
        
    static const _pixel3_formathash v3_pixel_formathash(v3_pixel_formathash_value, v3_pixel_formathash_value + PVR3_MAX_TABLE_ELEMENTS);
        
    typedef struct _PVRTexHeader
    {
        unsigned int headerLength;
        unsigned int height;
        unsigned int width;
        unsigned int numMipmaps;
        unsigned int flags;
        unsigned int dataLength;
        unsigned int bpp;
        unsigned int bitmaskRed;
        unsigned int bitmaskGreen;
        unsigned int bitmaskBlue;
        unsigned int bitmaskAlpha;
        unsigned int pvrTag;
        unsigned int numSurfs;
    } PVRv2TexHeader;
        
#ifdef _MSC_VER
#pragma pack(push,1)
#endif
    typedef struct
    {
        uint32_t version;
        uint32_t flags;
        uint64_t pixelFormat;
        uint32_t colorSpace;
        uint32_t channelType;
        uint32_t height;
        uint32_t width;
        uint32_t depth;
        uint32_t numberOfSurfaces;
        uint32_t numberOfFaces;
        uint32_t numberOfMipmaps;
        uint32_t metadataLength;
#ifdef _MSC_VER
    } PVRv3TexHeader;
#pragma pack(pop)
#else
    } __attribute__((packed)) PVRv3TexHeader;
#endif
}
//pvr structure end

//////////////////////////////////////////////////////////////////////////

//struct and data for s3tc(dds) struct
namespace
{
    struct DDColorKey
    {
        uint32_t colorSpaceLowValue;
        uint32_t colorSpaceHighValue;
    };
    
    struct DDSCaps
    {
        uint32_t caps;
        uint32_t caps2;
        uint32_t caps3;
        uint32_t caps4;
    };
    
    struct DDPixelFormat
    {
        uint32_t size;
        uint32_t flags;
        uint32_t fourCC;
        uint32_t RGBBitCount;
        uint32_t RBitMask;
        uint32_t GBitMask;
        uint32_t BBitMask;
        uint32_t ABitMask;
    };
    
    
    struct DDSURFACEDESC2
    {
        uint32_t size;
        uint32_t flags;
        uint32_t height;
        uint32_t width;
        
        union
        {
            uint32_t pitch;
            uint32_t linearSize;
        } DUMMYUNIONNAMEN1;
        
        union
        {
            uint32_t backBufferCount;
            uint32_t depth;
        } DUMMYUNIONNAMEN5;
        
        union
        {
            uint32_t mipMapCount;
            uint32_t refreshRate;
            uint32_t srcVBHandle;
        } DUMMYUNIONNAMEN2;
        
        uint32_t alphaBitDepth;
        uint32_t reserved;
        uint32_t surface;
        
        union
        {
            DDColorKey ddckCKDestOverlay;
            uint32_t emptyFaceColor;
        } DUMMYUNIONNAMEN3;
        
        DDColorKey ddckCKDestBlt;
        DDColorKey ddckCKSrcOverlay;
        DDColorKey ddckCKSrcBlt;
        
        union
        {
            DDPixelFormat ddpfPixelFormat;
            uint32_t FVF;
        } DUMMYUNIONNAMEN4;
        
        DDSCaps ddsCaps;
        uint32_t textureStage;
    } ;
    
#pragma pack(push,1)
    
    struct S3TCTexHeader
    {
        char fileCode[4];
        DDSURFACEDESC2 ddsd;
    };
    
#pragma pack(pop)

}
//s3tc struct end

//////////////////////////////////////////////////////////////////////////

//struct and data for atitc(ktx) struct
namespace
{
    struct ATITCTexHeader
    {
        //HEADER
        char identifier[12];
        uint32_t endianness;
        uint32_t glType;
        uint32_t glTypeSize;
        uint32_t glFormat;
        uint32_t glInternalFormat;
        uint32_t glBaseInternalFormat;
        uint32_t pixelWidth;
        uint32_t pixelHeight;
        uint32_t pixelDepth;
        uint32_t numberOfArrayElements;
        uint32_t numberOfFaces;
        uint32_t numberOfMipmapLevels;
        uint32_t bytesOfKeyValueData;
    };
}
//atitc struct end

//////////////////////////////////////////////////////////////////////////

namespace
{
    typedef struct 
    {
        const unsigned char * data;
        ssize_t size;
        int offset;
    }tImageSource;
 
#if CC_USE_PNG
    static void pngReadCallback(png_structp png_ptr, png_bytep data, png_size_t length)
    {
        tImageSource* isource = (tImageSource*)png_get_io_ptr(png_ptr);
        
        if((int)(isource->offset + length) <= isource->size)
        {
            memcpy(data, isource->data+isource->offset, length);
            isource->offset += length;
        }
        else
        {
            png_error(png_ptr, "pngReaderCallback failed");
        }
    }
#endif //CC_USE_PNG
}

Texture2D::PixelFormat getDevicePixelFormat(Texture2D::PixelFormat format)
{
    switch (format) {
        case Texture2D::PixelFormat::PVRTC4:
        case Texture2D::PixelFormat::PVRTC4A:
        case Texture2D::PixelFormat::PVRTC2:
        case Texture2D::PixelFormat::PVRTC2A:
            if(Configuration::getInstance()->supportsPVRTC())
                return format;
            else
                return Texture2D::PixelFormat::RGBA8888;
        case Texture2D::PixelFormat::ETC:
            if(Configuration::getInstance()->supportsETC())
                return format;
            else
                return Texture2D::PixelFormat::RGB888;
        default:
            return format;
    }
}

//////////////////////////////////////////////////////////////////////////
// Implement Image
//////////////////////////////////////////////////////////////////////////
bool Image::PNG_PREMULTIPLIED_ALPHA_ENABLED = true;

static const unsigned char gsc_default_image[] =
{
    0x89,0x50,0x4E,0x47,0x0D,0x0A,0x1A,0x0A,0x00,0x00,0x00,0x0D,0x49,0x48,0x44,0x52,
    0x00,0x00,0x00,0x50,0x00,0x00,0x00,0x50,0x08,0x06,0x00,0x00,0x00,0x8E,0x11,0xF2,
    0xAD,0x00,0x00,0x00,0x19,0x74,0x45,0x58,0x74,0x53,0x6F,0x66,0x74,0x77,0x61,0x72,
    0x65,0x00,0x41,0x64,0x6F,0x62,0x65,0x20,0x49,0x6D,0x61,0x67,0x65,0x52,0x65,0x61,
    0x64,0x79,0x71,0xC9,0x65,0x3C,0x00,0x00,0x05,0x36,0x49,0x44,0x41,0x54,0x78,0xDA,
    0xEC,0x9D,0x7D,0x4C,0x1B,0x75,0x18,0xC7,0x9F,0x52,0x38,0x56,0x28,0x6F,0x85,0x66,
    0x0C,0x46,0xC7,0x2C,0xA3,0x16,0xF0,0x95,0xE9,0xA6,0xE8,0x44,0xCD,0xA6,0x0B,0xBA,
    0x39,0x32,0xC6,0xA2,0xD1,0xC4,0x04,0xA3,0x59,0xFC,0x6B,0xFB,0xC7,0x04,0xFF,0x70,
    0x66,0xF1,0x35,0x71,0x7F,0xF8,0x9A,0xB8,0xC4,0x97,0x44,0xE7,0x4B,0x62,0x46,0x22,
    0x06,0x35,0x99,0xCE,0x60,0x16,0x89,0x9B,0x71,0xA8,0x9B,0xCA,0x26,0x32,0x51,0x04,
    0xB6,0x81,0x32,0x2A,0x74,0x50,0x7F,0xCF,0xD9,0x3A,0x28,0x77,0xD7,0xDF,0xB5,0x77,
    0xC7,0xDD,0xF5,0xF9,0x26,0x4F,0x42,0x48,0xD3,0x72,0x9F,0xFE,0x9E,0xD7,0xFB,0xDD,
    0x0F,0x47,0x24,0x12,0x01,0x52,0xF2,0xCA,0x8C,0xFD,0x90,0xFD,0xD2,0xD7,0x44,0x43,
    0xA5,0xA6,0x76,0xAC,0x86,0x0C,0xC2,0xA0,0xD1,0x0A,0x54,0xD0,0xB5,0xCC,0x5A,0x99,
    0xDD,0xC4,0xEC,0x32,0x66,0x42,0x1A,0x70,0x39,0xCB,0xEC,0x07,0x66,0x9F,0x32,0x7B,
    0x9D,0xD9,0x80,0xDC,0x0B,0x95,0x56,0xA0,0x87,0xD9,0xFB,0xCC,0xBE,0x62,0xB6,0x93,
    0x59,0x7D,0x9A,0xC0,0x8B,0x5D,0xFB,0x0D,0xCC,0x76,0x33,0x3B,0xC1,0x6C,0x0F,0x33,
    0xA7,0x1A,0x80,0x55,0x51,0x70,0x5B,0xC9,0x49,0xC1,0xC5,0xAC,0x9D,0x59,0x87,0xD4,
    0x02,0x92,0x02,0xE8,0x66,0xF6,0x41,0x14,0x22,0xE9,0xA2,0x9A,0x98,0xED,0xE3,0x01,
    0xD8,0x1E,0x8D,0x75,0xA4,0x85,0xBA,0x97,0xD9,0x7A,0x25,0x80,0xB8,0x5C,0x1F,0x26,
    0x4E,0x8A,0x6A,0x57,0x02,0xB8,0x25,0xEA,0xC2,0x24,0x79,0x61,0x72,0xF1,0xCA,0x01,
    0x24,0xD7,0x4D,0x2C,0xCC,0xC6,0x57,0xCA,0x01,0x2C,0x25,0x3E,0x5C,0xF2,0xC9,0x01,
    0x14,0x88,0x0D,0x97,0x04,0x9E,0x42,0x9A,0xC4,0x21,0x02,0x48,0x00,0xCD,0x3F,0x4C,
    0x58,0xA0,0xEA,0xC2,0x25,0xE0,0xCE,0xB2,0x1F,0xFB,0x99,0x08,0xC0,0xB7,0xA3,0x93,
    0xFA,0x02,0x6C,0xAB,0xF1,0xC2,0x8B,0x8D,0x2B,0x6C,0xBB,0xA2,0x5E,0x3B,0x3E,0x0A,
    0x0F,0x7D,0xD6,0xAF,0x9F,0x0B,0xB7,0x56,0x7B,0x6C,0xED,0x92,0xF7,0x04,0x8A,0xF5,
    0x8D,0x81,0x4E,0x87,0xC3,0xDE,0xF5,0x49,0x86,0x43,0x5F,0x17,0x8E,0xD7,0x27,0x03,
    0xE3,0xF0,0xFC,0xB1,0x61,0x18,0x0D,0x85,0x2D,0x07,0xCB,0x9D,0xE5,0x14,0x3D,0x0A,
    0xC3,0x92,0xA1,0x49,0x24,0xA6,0x93,0xE3,0x53,0x70,0x57,0x67,0x1F,0x0B,0xBE,0xD6,
    0xBD,0x31,0xF5,0xC5,0xEF,0x7F,0x43,0x69,0x4E,0x16,0xDC,0x51,0x59,0x68,0x7C,0x19,
    0x33,0x34,0x19,0xB6,0x34,0xBC,0x98,0x06,0x27,0x92,0xF7,0x1E,0xAA,0x03,0xA9,0x90,
    0xB6,0x60,0x21,0x9D,0x8C,0x96,0xB2,0x38,0x73,0xB5,0x37,0x07,0x3C,0x4B,0x32,0xE1,
    0xE8,0xF0,0x24,0x1C,0x3F,0x17,0x22,0x80,0x7C,0x99,0x2E,0x03,0x9E,0x6E,0xA8,0x58,
    0x90,0xE9,0xBE,0x3B,0x13,0x82,0x07,0x0E,0xFE,0x02,0x47,0x47,0x26,0xC9,0x85,0xE5,
    0x54,0x20,0x38,0xE1,0x70,0x4B,0x8D,0x64,0x99,0x50,0x57,0xEC,0x82,0xEE,0xAD,0x41,
    0xB8,0xB5,0x22,0x9F,0x00,0xCA,0x69,0xF7,0x9A,0x72,0xB1,0x6F,0x56,0x2A,0xCA,0xF7,
    0xDD,0x52,0xA9,0xBA,0x78,0x4D,0x0B,0x80,0x08,0x87,0xA7,0xED,0x2B,0xCB,0x15,0x2C,
    0xBD,0x0A,0x75,0x03,0x58,0x96,0x9B,0x05,0x9E,0x6C,0xBE,0x10,0x1B,0xF4,0xB8,0x08,
    0x60,0xBC,0xA6,0x67,0xF9,0x0B,0xEC,0xE9,0x99,0x08,0x01,0x8C,0xD7,0x9F,0xAC,0x4B,
    0x39,0x3D,0x31,0xCD,0xF5,0xDA,0x6F,0x46,0xCE,0x13,0x40,0x29,0x3D,0x7A,0xF8,0xB7,
    0x84,0xAF,0xF9,0xB0,0x7F,0x0C,0xBE,0xFC,0x63,0x82,0x00,0x4A,0xE9,0x9D,0x9F,0xCF,
    0x8A,0x03,0x4A,0x39,0xF5,0xFF,0x35,0x05,0x3B,0x3E,0xFF,0x35,0xE9,0xF7,0x5F,0x57,
    0x96,0x07,0xFE,0x82,0x6C,0x7B,0xB7,0x72,0x38,0xDD,0xDD,0xD6,0x75,0x52,0x84,0x15,
    0xD3,0x44,0x78,0x16,0x5E,0xEE,0x1D,0x86,0xFA,0x77,0xBF,0x17,0x5D,0x3D,0x19,0x61,
    0x82,0x7A,0x63,0xFD,0x4A,0x38,0xD2,0x5A,0x2B,0x96,0x4B,0xAE,0xCC,0xC5,0xE9,0x4A,
    0x0D,0x69,0xE5,0x3A,0x4E,0x9D,0x13,0xCD,0xEB,0xCA,0x84,0x22,0x76,0xE1,0x3F,0x8D,
    0xFD,0x93,0xF2,0x7B,0x3E,0xC6,0xA0,0x61,0x09,0x84,0x7A,0xA4,0x7E,0x99,0x38,0x49,
    0xDE,0xD5,0x7D,0x5A,0xFC,0x1C,0xDB,0x0E,0x13,0x46,0x42,0x17,0x34,0x81,0x77,0x45,
    0x49,0x0E,0xB4,0xD5,0x96,0xCC,0xFB,0x5D,0x85,0x5B,0x80,0xF7,0x6E,0xF7,0xC3,0x47,
    0x77,0x56,0x2B,0x16,0xEF,0x96,0x06,0xA8,0x95,0xF6,0xDE,0xE8,0x93,0xBD,0xB5,0x80,
    0x45,0xF9,0x91,0xED,0xB5,0xF0,0xE4,0x75,0xCB,0x0D,0xB9,0x73,0x68,0x39,0x80,0xDB,
    0x57,0x79,0xA0,0x61,0x99,0xF2,0x06,0x32,0x6C,0x0D,0x77,0x5E,0x55,0x0A,0xBD,0x77,
    0xD7,0xC1,0xC6,0x15,0x05,0x04,0x70,0x6E,0xE2,0xD8,0xBB,0xCE,0xA7,0xA2,0x1B,0x12,
    0x60,0xCF,0xDA,0xE5,0x04,0x70,0x6E,0xE2,0xE0,0x6D,0x0F,0x63,0xC2,0xA9,0x0F,0x5A,
    0xDA,0x03,0x94,0x4A,0x1C,0xBC,0x6A,0xF6,0x17,0x11,0x40,0xA5,0xC4,0x41,0x00,0x13,
    0xE8,0xFE,0x60,0x49,0xC2,0xC4,0xA1,0xA4,0x60,0x91,0x7E,0x6E,0x6C,0x7A,0x80,0x18,
    0xF3,0x9E,0xB8,0x3E,0xF5,0x44,0xD0,0x52,0xE5,0xB1,0x0F,0x40,0xEC,0x48,0xF4,0x4C,
    0x1C,0x46,0xBA,0xB1,0xE1,0x00,0xB1,0x63,0xE8,0xDA,0x14,0x80,0x03,0x4D,0xAB,0x12,
    0x82,0x4C,0x25,0x71,0xC4,0x0B,0xBB,0x13,0x3D,0xDC,0xD8,0x50,0x80,0x38,0x3D,0xE9,
    0x69,0xAD,0x11,0x2F,0x04,0x0B,0xDC,0x9E,0x6D,0x35,0xD0,0x58,0x9E,0x27,0xF9,0x5A,
    0x4C,0x18,0xA9,0x24,0x0E,0xE9,0x22,0xBC,0xD8,0xBA,0x00,0xEF,0xBB,0xB4,0x04,0x3A,
    0x37,0x55,0xCF,0x73,0x47,0x2C,0x74,0x3F,0xDE,0x1C,0x10,0xA7,0x29,0xF1,0xA0,0x70,
    0xE5,0xA5,0x92,0x38,0x8C,0x72,0x63,0xDD,0x01,0x22,0x98,0x67,0x1A,0x2A,0xE0,0x55,
    0x85,0xBB,0x6F,0x38,0x4D,0x39,0xB8,0x25,0x00,0x95,0xF9,0xD9,0xFF,0x27,0x0E,0x8C,
    0x7D,0x5A,0x0B,0x67,0x87,0x18,0x16,0xB4,0x94,0xAE,0xE3,0x2C,0x6C,0xE6,0xF7,0xDF,
    0xE6,0x87,0x0D,0xBE,0xC4,0xFD,0xE8,0xDA,0x52,0xB7,0xE8,0xD2,0x38,0x3F,0x6C,0x2C,
    0xCF,0xD7,0x24,0x71,0xC8,0x65,0x63,0xB5,0xDB,0x78,0x17,0x65,0x05,0xE2,0xB7,0x7D,
    0xA8,0x39,0xC8,0x05,0x2F,0x26,0xBC,0x11,0x8F,0xC0,0x1F,0xAC,0xF3,0xEA,0xF6,0xA5,
    0xB6,0x54,0x15,0x99,0xDF,0x85,0x31,0x59,0xE0,0xAE,0x03,0x3D,0x7B,0xD0,0x64,0x85,
    0x61,0x02,0xF7,0xE8,0x98,0x16,0x20,0x76,0x0D,0xF1,0xC9,0xC2,0x6C,0x6A,0xF6,0x7B,
    0xCC,0x09,0xB0,0x7D,0x75,0x19,0xBC,0x72,0xB3,0xF9,0xB7,0x6A,0x6C,0xBE,0xA4,0xD0,
    0x9C,0x00,0xAF,0x59,0x9A,0x6B,0x89,0xC1,0x84,0x96,0x37,0xA0,0x68,0x83,0x25,0x01,
    0x5C,0x5C,0x69,0x1A,0xE9,0x77,0x75,0x0F,0xC0,0xE3,0x3D,0x4E,0xD3,0x5F,0xB4,0x9A,
    0x7D,0x3B,0x86,0x02,0xC4,0xC7,0x1E,0xC8,0x85,0x55,0xC8,0x97,0x27,0xD8,0xE2,0xC9,
    0xA5,0x95,0xF9,0xC2,0xE2,0xAC,0x40,0x1C,0x4D,0x1D,0x68,0xAA,0x82,0x8E,0x53,0x63,
    0x96,0x85,0x77,0x39,0xEB,0x8D,0xD5,0x74,0x4B,0x9A,0xBB,0x30,0x7E,0x78,0x2A,0x7F,
    0x40,0xDA,0xB9,0x70,0x78,0xD6,0xDE,0xE7,0x0D,0x86,0x2E,0xCC,0xEA,0x0B,0xF0,0xCD,
    0x13,0xA3,0xE2,0xEE,0x2A,0x3B,0x0A,0x1F,0x5B,0x7B,0xE1,0xD8,0xB0,0xBE,0x2E,0xFC,
    0xD6,0x8F,0x67,0xC4,0x1D,0x50,0x46,0x6E,0xE0,0x31,0x4A,0x83,0xE7,0xC3,0xAA,0xB7,
    0xDB,0x25,0x15,0x03,0x71,0x05,0x5A,0xFD,0x01,0x19,0xBD,0x62,0xE0,0x0C,0x21,0xE1,
    0xAB,0xC5,0xE5,0x00,0x0E,0x12,0x1B,0x2E,0x0D,0xC9,0x01,0xEC,0x25,0x36,0x5C,0xEA,
    0x95,0x03,0xD8,0x35,0x77,0x79,0x92,0x24,0x75,0x08,0xE6,0x9C,0xA9,0x1A,0x0F,0x10,
    0x0F,0x5F,0x7D,0x8E,0x18,0x29,0xEA,0xD9,0x44,0x75,0xE0,0x53,0xCC,0xFA,0x88,0x93,
    0xA4,0xDE,0x66,0xD6,0x99,0x08,0xE0,0x38,0xB3,0x8D,0xA0,0x70,0xF4,0x6F,0x1A,0xBB,
    0x6E,0x1B,0x6F,0x27,0x82,0x2B,0x70,0x4D,0x34,0x26,0x52,0xC9,0xF2,0x5F,0x58,0xDB,
    0x80,0x9D,0x9E,0x9A,0x42,0x7A,0x28,0xBA,0x12,0xF1,0xC8,0x4B,0x3C,0x7C,0x15,0x0F,
    0xE2,0x0E,0xA4,0x11,0xB8,0x1E,0xB8,0x78,0x10,0x77,0x5F,0x2A,0x9D,0x48,0x77,0xD4,
    0x48,0x5A,0x0C,0x13,0x48,0xF3,0xE5,0xA0,0x7F,0x87,0x91,0x9A,0xFE,0x15,0x60,0x00,
    0xB8,0xC0,0x2E,0x5E,0x19,0xEB,0xFC,0x72,0x00,0x00,0x00,0x00,0x49,0x45,0x4E,0x44,
    0xAE,0x42,0x60,0x82,
};

Image::Image()
: _data(nullptr)
, _dataLen(0)
, _width(0)
, _height(0)
, _unpack(false)
, _fileType(Format::UNKNOWN)
, _renderFormat(Texture2D::PixelFormat::NONE)
, _numberOfMipmaps(0)
, _hasPremultipliedAlpha(false)
{

}

Image::~Image()
{
    if(_unpack)
    {
        for (int i = 0; i < _numberOfMipmaps; ++i)
            CC_SAFE_DELETE_ARRAY(_mipmaps[i].address);
    }
    else
        CC_SAFE_FREE(_data);
}

bool Image::initWithImageFile(const std::string& path)
{
    bool ret = false;
    _filePath = FileUtils::getInstance()->fullPathForFilename(path);

    Data data = FileUtils::getInstance()->getDataFromFile(_filePath);

    if (!data.isNull())
    {
        ret = initWithImageData(data.getBytes(), data.getSize());
    }

    return ret;
}

bool Image::initWithImageFileThreadSafe(const std::string& fullpath)
{
    bool ret = false;
    _filePath = fullpath;

    Data data = FileUtils::getInstance()->getDataFromFile(fullpath);

    if (!data.isNull())
    {
        ret = initWithImageData(data.getBytes(), data.getSize());
    }

    return ret;
}

bool Image::initWithImageData(const unsigned char * data, ssize_t dataLen)
{
    bool ret = false;
    
    do
    {
        if (data == nullptr)
        {
            data = gsc_default_image;
            dataLen = sizeof(gsc_default_image);
        }

        CC_BREAK_IF(! data || dataLen <= 0);
        
        unsigned char* unpackedData = nullptr;
        ssize_t unpackedLen = 0;
        //处理加密图片 Add By Y-way 2016-03-09
        unsigned char* tmpUnpackerData = (unsigned char*)data;
        ssize_t tmpPackLen = dataLen;
        unsigned char* pTmpData = (unsigned char*)data;
        NSwfHeader nswfHeader;
        memcpy((void*)(&nswfHeader), pTmpData, sizeof(nswfHeader));
        
        if (nswfHeader.isSigned())//已经加密
        {
            unsigned char* tmpData = pTmpData + nswfHeader.offset;
            for (int i = 0; i < 16; i++)
            {
                *tmpData = ((*tmpData << 0x04) | (*tmpData >> 0x04));
                tmpData++;
        }
            tmpUnpackerData = (pTmpData + nswfHeader.offset);
            tmpPackLen = nswfHeader.fLen;
        }
        //Add End

        //detecgt and unzip the compress file
        if (ZipUtils::isCCZBuffer(tmpUnpackerData, tmpPackLen))
        {
            unpackedLen = ZipUtils::inflateCCZBuffer(tmpUnpackerData, tmpPackLen, &unpackedData);
        }
        else if (ZipUtils::isGZipBuffer(tmpUnpackerData, tmpPackLen))
        {
            unpackedLen = ZipUtils::inflateMemory(const_cast<unsigned char*>(tmpUnpackerData), tmpPackLen, &unpackedData);
        }
        else
        {
            unpackedData = const_cast<unsigned char*>(tmpUnpackerData);
            unpackedLen = tmpPackLen;
        }

        _fileType = detectFormat(unpackedData, unpackedLen);

        switch (_fileType)
        {
        case Format::PNG:
            ret = initWithPngData(unpackedData, unpackedLen);
            break;
        case Format::JPG:
            ret = initWithJpgData(unpackedData, unpackedLen);
            break;
        case Format::TIFF:
            ret = initWithTiffData(unpackedData, unpackedLen);
            break;
        case Format::WEBP:
            ret = initWithWebpData(unpackedData, unpackedLen);
            break;
        case Format::PVR:
            ret = initWithPVRData(unpackedData, unpackedLen);
            break;
        case Format::ETC:
            ret = initWithETCData(unpackedData, unpackedLen);
            break;
        case Format::S3TC:
            ret = initWithS3TCData(unpackedData, unpackedLen);
            break;
        case Format::ATITC:
            ret = initWithATITCData(unpackedData, unpackedLen);
            break;
        default:
            {
                // load and detect image format
                tImageTGA* tgaData = tgaLoadBuffer(unpackedData, unpackedLen);
                
                if (tgaData != nullptr && tgaData->status == TGA_OK)
                {
                    ret = initWithTGAData(tgaData);
                }
                else
                {
                    CCLOG("cocos2d: unsupported image format!");
                }
                
                free(tgaData);
                break;
            }
        }
        
        if(unpackedData != tmpUnpackerData)
        {
            free(unpackedData);
        }
    } while (0);
    
    return ret;
}

bool Image::isPng(const unsigned char * data, ssize_t dataLen)
{
    if (dataLen <= 8)
    {
        return false;
    }

    static const unsigned char PNG_SIGNATURE[] = {0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a};

    return memcmp(PNG_SIGNATURE, data, sizeof(PNG_SIGNATURE)) == 0;
}


bool Image::isEtc(const unsigned char * data, ssize_t /*dataLen*/)
{
    return etc1_pkm_is_valid((etc1_byte*)data) ? true : false;
}


bool Image::isS3TC(const unsigned char * data, ssize_t /*dataLen*/)
{

    S3TCTexHeader *header = (S3TCTexHeader *)data;
    
    if (strncmp(header->fileCode, "DDS", 3) != 0)
    {
        return false;
    }
    return true;
}

bool Image::isATITC(const unsigned char *data, ssize_t /*dataLen*/)
{
    ATITCTexHeader *header = (ATITCTexHeader *)data;
    
    if (strncmp(&header->identifier[1], "KTX", 3) != 0)
    {
        return false;
    }
    return true;
}

bool Image::isJpg(const unsigned char * data, ssize_t dataLen)
{
    if (dataLen <= 4)
    {
        return false;
    }

    static const unsigned char JPG_SOI[] = {0xFF, 0xD8};

    return memcmp(data, JPG_SOI, 2) == 0;
}

bool Image::isTiff(const unsigned char * data, ssize_t dataLen)
{
    if (dataLen <= 4)
    {
        return false;
    }

    static const char* TIFF_II = "II";
    static const char* TIFF_MM = "MM";

    return (memcmp(data, TIFF_II, 2) == 0 && *(static_cast<const unsigned char*>(data) + 2) == 42 && *(static_cast<const unsigned char*>(data) + 3) == 0) ||
        (memcmp(data, TIFF_MM, 2) == 0 && *(static_cast<const unsigned char*>(data) + 2) == 0 && *(static_cast<const unsigned char*>(data) + 3) == 42);
}

bool Image::isWebp(const unsigned char * data, ssize_t dataLen)
{
    if (dataLen <= 12)
    {
        return false;
    }

    static const char* WEBP_RIFF = "RIFF";
    static const char* WEBP_WEBP = "WEBP";

    return memcmp(data, WEBP_RIFF, 4) == 0 
        && memcmp(static_cast<const unsigned char*>(data) + 8, WEBP_WEBP, 4) == 0;
}

bool Image::isPvr(const unsigned char * data, ssize_t dataLen)
{
    if (static_cast<size_t>(dataLen) < sizeof(PVRv2TexHeader) || static_cast<size_t>(dataLen) < sizeof(PVRv3TexHeader))
    {
        return false;
    }
    
    const PVRv2TexHeader* headerv2 = static_cast<const PVRv2TexHeader*>(static_cast<const void*>(data));
    const PVRv3TexHeader* headerv3 = static_cast<const PVRv3TexHeader*>(static_cast<const void*>(data));
    
    return memcmp(&headerv2->pvrTag, gPVRTexIdentifier, strlen(gPVRTexIdentifier)) == 0 || CC_SWAP_INT32_BIG_TO_HOST(headerv3->version) == 0x50565203;
}

Image::Format Image::detectFormat(const unsigned char * data, ssize_t dataLen)
{
    if (isPng(data, dataLen))
    {
        return Format::PNG;
    }
    else if (isJpg(data, dataLen))
    {
        return Format::JPG;
    }
    else if (isTiff(data, dataLen))
    {
        return Format::TIFF;
    }
    else if (isWebp(data, dataLen))
    {
        return Format::WEBP;
    }
    else if (isPvr(data, dataLen))
    {
        return Format::PVR;
    }
    else if (isEtc(data, dataLen))
    {
        return Format::ETC;
    }
    else if (isS3TC(data, dataLen))
    {
        return Format::S3TC;
    }
    else if (isATITC(data, dataLen))
    {
        return Format::ATITC;
    }
    else
    {
        return Format::UNKNOWN;
    }
}

int Image::getBitPerPixel()
{
    return Texture2D::getPixelFormatInfoMap().at(_renderFormat).bpp;
}

bool Image::hasAlpha()
{
    return Texture2D::getPixelFormatInfoMap().at(_renderFormat).alpha;
}

bool Image::isCompressed()
{
    return Texture2D::getPixelFormatInfoMap().at(_renderFormat).compressed;
}

namespace
{
/*
 * ERROR HANDLING:
 *
 * The JPEG library's standard error handler (jerror.c) is divided into
 * several "methods" which you can override individually.  This lets you
 * adjust the behavior without duplicating a lot of code, which you might
 * have to update with each future release.
 *
 * We override the "error_exit" method so that control is returned to the
 * library's caller when a fatal error occurs, rather than calling exit()
 * as the standard error_exit method does.
 *
 * We use C's setjmp/longjmp facility to return control.  This means that the
 * routine which calls the JPEG library must first execute a setjmp() call to
 * establish the return point.  We want the replacement error_exit to do a
 * longjmp().  But we need to make the setjmp buffer accessible to the
 * error_exit routine.  To do this, we make a private extension of the
 * standard JPEG error handler object.  (If we were using C++, we'd say we
 * were making a subclass of the regular error handler.)
 *
 * Here's the extended error handler struct:
 */
#if CC_USE_JPEG
    struct MyErrorMgr
    {
        struct jpeg_error_mgr pub;  /* "public" fields */
        jmp_buf setjmp_buffer;  /* for return to caller */
    };
    
    typedef struct MyErrorMgr * MyErrorPtr;
    
    /*
     * Here's the routine that will replace the standard error_exit method:
     */
    
    METHODDEF(void)
    myErrorExit(j_common_ptr cinfo)
    {
        /* cinfo->err really points to a MyErrorMgr struct, so coerce pointer */
        MyErrorPtr myerr = (MyErrorPtr) cinfo->err;
        
        /* Always display the message. */
        /* We could postpone this until after returning, if we chose. */
        /* internal message function can't show error message in some platforms, so we rewrite it here.
         * edit it if has version conflict.
         */
        //(*cinfo->err->output_message) (cinfo);
        char buffer[JMSG_LENGTH_MAX];
        (*cinfo->err->format_message) (cinfo, buffer);
        CCLOG("jpeg error: %s", buffer);
        
        /* Return control to the setjmp point */
        longjmp(myerr->setjmp_buffer, 1);
    }
#endif // CC_USE_JPEG
}

#if CC_USE_WIC
bool Image::decodeWithWIC(const unsigned char *data, ssize_t dataLen)
{
    bool bRet = false;
    WICImageLoader img;

    if (img.decodeImageData(data, dataLen))
    {
        _width = img.getWidth();
        _height = img.getHeight();

        WICPixelFormatGUID format = img.getPixelFormat();

        if (memcmp(&format, &GUID_WICPixelFormat8bppGray, sizeof(WICPixelFormatGUID)) == 0)
        {
            _renderFormat = Texture2D::PixelFormat::I8;
        }

        if (memcmp(&format, &GUID_WICPixelFormat8bppAlpha, sizeof(WICPixelFormatGUID)) == 0)
        {
            _renderFormat = Texture2D::PixelFormat::AI88;
        }

        if (memcmp(&format, &GUID_WICPixelFormat24bppRGB, sizeof(WICPixelFormatGUID)) == 0)
        {
            _renderFormat = Texture2D::PixelFormat::RGB888;
        }

        if (memcmp(&format, &GUID_WICPixelFormat32bppRGBA, sizeof(WICPixelFormatGUID)) == 0)
        {
            _renderFormat = Texture2D::PixelFormat::RGBA8888;
        }

        if (memcmp(&format, &GUID_WICPixelFormat32bppBGRA, sizeof(WICPixelFormatGUID)) == 0)
        {
            _renderFormat = Texture2D::PixelFormat::BGRA8888;
        }

        _dataLen = img.getImageDataSize();

        CCASSERT(_dataLen > 0, "Image: Decompressed data length is invalid");

        _data = new (std::nothrow) unsigned char[_dataLen];
        bRet = (img.getImageData(_data, _dataLen) > 0);

        if (_renderFormat == Texture2D::PixelFormat::RGBA8888) {
            premultipliedAlpha();
        }
    }

    return bRet;
}

bool Image::encodeWithWIC(const std::string& filePath, bool isToRGB, GUID containerFormat)
{
    // Save formats supported by WIC
    WICPixelFormatGUID targetFormat = isToRGB ? GUID_WICPixelFormat24bppBGR : GUID_WICPixelFormat32bppBGRA;
    unsigned char *pSaveData = nullptr;
    int saveLen = _dataLen;
    int bpp = 4;

    if (targetFormat == GUID_WICPixelFormat24bppBGR && _renderFormat == Texture2D::PixelFormat::RGBA8888)
    {
        bpp = 3;
        saveLen = _width * _height * bpp;
        pSaveData = new (std::nothrow) unsigned char[saveLen];
        int indL = 0, indR = 0;

        while (indL < saveLen && indR < _dataLen)
        {
            memcpy(&pSaveData[indL], &_data[indR], 3);
            indL += 3;
            indR += 4;
        }
    }
    else
    {
        pSaveData = new (std::nothrow) unsigned char[saveLen];
        memcpy(pSaveData, _data, saveLen);
    }

    for (int ind = 2; ind < saveLen; ind += bpp) {
        std::swap(pSaveData[ind - 2], pSaveData[ind]);
    }

    WICImageLoader img;
    bool bRet = img.encodeImageData(filePath, pSaveData, saveLen, targetFormat, _width, _height, containerFormat);

    delete[] pSaveData;
    return bRet;
}


#endif //CC_USE_WIC

bool Image::initWithJpgData(const unsigned char * data, ssize_t dataLen)
{
#if CC_USE_WIC
    return decodeWithWIC(data, dataLen);
#elif CC_USE_JPEG
    /* these are standard libjpeg structures for reading(decompression) */
    struct jpeg_decompress_struct cinfo;
    /* We use our private extension JPEG error handler.
     * Note that this struct must live as long as the main JPEG parameter
     * struct, to avoid dangling-pointer problems.
     */
    struct MyErrorMgr jerr;
    /* libjpeg data structure for storing one row, that is, scanline of an image */
    JSAMPROW row_pointer[1] = {0};
    unsigned long location = 0;

    bool ret = false;
    do 
    {
        /* We set up the normal JPEG error routines, then override error_exit. */
        cinfo.err = jpeg_std_error(&jerr.pub);
        jerr.pub.error_exit = myErrorExit;
        /* Establish the setjmp return context for MyErrorExit to use. */
        if (setjmp(jerr.setjmp_buffer))
        {
            /* If we get here, the JPEG code has signaled an error.
             * We need to clean up the JPEG object, close the input file, and return.
             */
            jpeg_destroy_decompress(&cinfo);
            break;
        }

        /* setup decompression process and source, then read JPEG header */
        jpeg_create_decompress( &cinfo );

#ifndef CC_TARGET_QT5
        jpeg_mem_src(&cinfo, const_cast<unsigned char*>(data), dataLen);
#endif /* CC_TARGET_QT5 */

        /* reading the image header which contains image information */
#if (JPEG_LIB_VERSION >= 90)
        // libjpeg 0.9 adds stricter types.
        jpeg_read_header(&cinfo, TRUE);
#else
        jpeg_read_header(&cinfo, TRUE);
#endif

        // we only support RGB or grayscale
        if (cinfo.jpeg_color_space == JCS_GRAYSCALE)
        {
            _renderFormat = Texture2D::PixelFormat::I8;
        }else
        {
            cinfo.out_color_space = JCS_RGB;
            _renderFormat = Texture2D::PixelFormat::RGB888;
        }

        /* Start decompression jpeg here */
        jpeg_start_decompress( &cinfo );

        /* init image info */
        _width  = cinfo.output_width;
        _height = cinfo.output_height;

        _dataLen = cinfo.output_width*cinfo.output_height*cinfo.output_components;
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        CC_BREAK_IF(! _data);

        /* now actually read the jpeg into the raw buffer */
        /* read one scan line at a time */
        while (cinfo.output_scanline < cinfo.output_height)
        {
            row_pointer[0] = _data + location;
            location += cinfo.output_width*cinfo.output_components;
            jpeg_read_scanlines(&cinfo, row_pointer, 1);
        }

    /* When read image file with broken data, jpeg_finish_decompress() may cause error.
     * Besides, jpeg_destroy_decompress() shall deallocate and release all memory associated
     * with the decompression object.
     * So it doesn't need to call jpeg_finish_decompress().
     */
    //jpeg_finish_decompress( &cinfo );
        jpeg_destroy_decompress( &cinfo );
        /* wrap up decompression, destroy objects, free pointers and close open files */        
        ret = true;
    } while (0);

    return ret;
#else
    CCLOG("jpeg is not enabled, please enable it in ccConfig.h");
    return false;
#endif // CC_USE_JPEG
}

bool Image::initWithPngData(const unsigned char * data, ssize_t dataLen)
{
#if CC_USE_WIC
    return decodeWithWIC(data, dataLen);
#elif CC_USE_PNG
    // length of bytes to check if it is a valid png file
#define PNGSIGSIZE  8
    bool ret = false;
    png_byte        header[PNGSIGSIZE]   = {0}; 
    png_structp     png_ptr     =   0;
    png_infop       info_ptr    = 0;

    do 
    {
        // png header len is 8 bytes
        CC_BREAK_IF(dataLen < PNGSIGSIZE);

        // check the data is png or not
        memcpy(header, data, PNGSIGSIZE);
        CC_BREAK_IF(png_sig_cmp(header, 0, PNGSIGSIZE));

        // init png_struct
        png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, 0, 0, 0);
        CC_BREAK_IF(! png_ptr);

        // init png_info
        info_ptr = png_create_info_struct(png_ptr);
        CC_BREAK_IF(!info_ptr);

#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA && CC_TARGET_PLATFORM != CC_PLATFORM_NACL && CC_TARGET_PLATFORM != CC_PLATFORM_TIZEN)
        CC_BREAK_IF(setjmp(png_jmpbuf(png_ptr)));
#endif

        // set the read call back function
        tImageSource imageSource;
        imageSource.data    = (unsigned char*)data;
        imageSource.size    = dataLen;
        imageSource.offset  = 0;
        png_set_read_fn(png_ptr, &imageSource, pngReadCallback);

        // read png header info

        // read png file info
        png_read_info(png_ptr, info_ptr);

        _width = png_get_image_width(png_ptr, info_ptr);
        _height = png_get_image_height(png_ptr, info_ptr);
        png_byte bit_depth = png_get_bit_depth(png_ptr, info_ptr);
        png_uint_32 color_type = png_get_color_type(png_ptr, info_ptr);

        //CCLOG("color type %u", color_type);

        // force palette images to be expanded to 24-bit RGB
        // it may include alpha channel
        if (color_type == PNG_COLOR_TYPE_PALETTE)
        {
            png_set_palette_to_rgb(png_ptr);
        }
        // low-bit-depth grayscale images are to be expanded to 8 bits
        if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        {
            bit_depth = 8;
            png_set_expand_gray_1_2_4_to_8(png_ptr);
        }
        // expand any tRNS chunk data into a full alpha channel
        if (png_get_valid(png_ptr, info_ptr, PNG_INFO_tRNS))
        {
            png_set_tRNS_to_alpha(png_ptr);
        }  
        // reduce images with 16-bit samples to 8 bits
        if (bit_depth == 16)
        {
            png_set_strip_16(png_ptr);            
        } 

        // Expanded earlier for grayscale, now take care of palette and rgb
        if (bit_depth < 8)
        {
            png_set_packing(png_ptr);
        }
        // update info
        png_read_update_info(png_ptr, info_ptr);
        color_type = png_get_color_type(png_ptr, info_ptr);

        switch (color_type)
        {
        case PNG_COLOR_TYPE_GRAY:
            _renderFormat = Texture2D::PixelFormat::I8;
            break;
        case PNG_COLOR_TYPE_GRAY_ALPHA:
            _renderFormat = Texture2D::PixelFormat::AI88;
            break;
        case PNG_COLOR_TYPE_RGB:
            _renderFormat = Texture2D::PixelFormat::RGB888;
            break;
        case PNG_COLOR_TYPE_RGB_ALPHA:
            _renderFormat = Texture2D::PixelFormat::RGBA8888;
            break;
        default:
            break;
        }

        // read png data
        png_size_t rowbytes;
        png_bytep* row_pointers = (png_bytep*)malloc( sizeof(png_bytep) * _height );

        rowbytes = png_get_rowbytes(png_ptr, info_ptr);

        _dataLen = rowbytes * _height;
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        if (!_data)
        {
            if (row_pointers != nullptr)
            {
                free(row_pointers);
            }
            break;
        }

        for (unsigned short i = 0; i < _height; ++i)
        {
            row_pointers[i] = _data + i*rowbytes;
        }
        png_read_image(png_ptr, row_pointers);

        png_read_end(png_ptr, nullptr);

        // premultiplied alpha for RGBA8888
        if (PNG_PREMULTIPLIED_ALPHA_ENABLED && color_type == PNG_COLOR_TYPE_RGB_ALPHA)
        {
            premultipliedAlpha();
        }

        if (row_pointers != nullptr)
        {
            free(row_pointers);
        }

        ret = true;
    } while (0);

    if (png_ptr)
    {
        png_destroy_read_struct(&png_ptr, (info_ptr) ? &info_ptr : 0, 0);
    }
    return ret;
#else
    CCLOG("png is not enabled, please enable it in ccConfig.h");
    return false;
#endif //CC_USE_PNG
}

#if CC_USE_TIFF
namespace
{
    static tmsize_t tiffReadProc(thandle_t fd, void* buf, tmsize_t size)
    {
        tImageSource* isource = (tImageSource*)fd;
        uint8* ma;
        uint64 mb;
        unsigned long n;
        unsigned long o;
        tmsize_t p;
        ma=(uint8*)buf;
        mb=size;
        p=0;
        while (mb>0)
        {
            n=0x80000000UL;
            if ((uint64)n>mb)
                n=(unsigned long)mb;
            
            
            if ((int)(isource->offset + n) <= isource->size)
            {
                memcpy(ma, isource->data+isource->offset, n);
                isource->offset += n;
                o = n;
            }
            else
            {
                return 0;
            }
            
            ma+=o;
            mb-=o;
            p+=o;
            if (o!=n)
            {
                break;
            }
        }
        return p;
    }
    
    static tmsize_t tiffWriteProc(thandle_t /*fd*/, void* /*buf*/, tmsize_t /*size*/)
    {
        return 0;
    }
    
    
    static uint64 tiffSeekProc(thandle_t fd, uint64 off, int whence)
    {
        tImageSource* isource = (tImageSource*)fd;
        uint64 ret = -1;
        do
        {
            if (whence == SEEK_SET)
            {
                CC_BREAK_IF(off >= (uint64)isource->size);
                ret = isource->offset = (uint32)off;
            }
            else if (whence == SEEK_CUR)
            {
                CC_BREAK_IF(isource->offset + off >= (uint64)isource->size);
                ret = isource->offset += (uint32)off;
            }
            else if (whence == SEEK_END)
            {
                CC_BREAK_IF(off >= (uint64)isource->size);
                ret = isource->offset = (uint32)(isource->size-1 - off);
            }
            else
            {
                CC_BREAK_IF(off >= (uint64)isource->size);
                ret = isource->offset = (uint32)off;
            }
        } while (0);
        
        return ret;
    }
    
    static uint64 tiffSizeProc(thandle_t fd)
    {
        tImageSource* imageSrc = (tImageSource*)fd;
        return imageSrc->size;
    }
    
    static int tiffCloseProc(thandle_t /*fd*/)
    {
        return 0;
    }
    
    static int tiffMapProc(thandle_t /*fd*/, void** /*base*/, toff_t* /*size*/)
    {
        return 0;
    }
    
    static void tiffUnmapProc(thandle_t /*fd*/, void* /*base*/, toff_t /*size*/)
    {
    }
}
#endif // CC_USE_TIFF

bool Image::initWithTiffData(const unsigned char * data, ssize_t dataLen)
{
#if CC_USE_WIC
    return decodeWithWIC(data, dataLen);
#elif CC_USE_TIFF
    bool ret = false;
    do 
    {
        // set the read call back function
        tImageSource imageSource;
        imageSource.data    = data;
        imageSource.size    = dataLen;
        imageSource.offset  = 0;

        TIFF* tif = TIFFClientOpen("file.tif", "r", (thandle_t)&imageSource, 
            tiffReadProc, tiffWriteProc,
            tiffSeekProc, tiffCloseProc, tiffSizeProc,
            tiffMapProc,
            tiffUnmapProc);

        CC_BREAK_IF(nullptr == tif);

        uint32 w = 0, h = 0;
        uint16 bitsPerSample = 0, samplePerPixel = 0, planarConfig = 0;
        size_t npixels = 0;
        
        TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w);
        TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h);
        TIFFGetField(tif, TIFFTAG_BITSPERSAMPLE, &bitsPerSample);
        TIFFGetField(tif, TIFFTAG_SAMPLESPERPIXEL, &samplePerPixel);
        TIFFGetField(tif, TIFFTAG_PLANARCONFIG, &planarConfig);

        npixels = w * h;
        
        _renderFormat = Texture2D::PixelFormat::RGBA8888;
        _width = w;
        _height = h;

        _dataLen = npixels * sizeof (uint32);
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));

        uint32* raster = (uint32*) _TIFFmalloc(npixels * sizeof (uint32));
        if (raster != nullptr) 
        {
           if (TIFFReadRGBAImageOriented(tif, w, h, raster, ORIENTATION_TOPLEFT, 0))
           {
                /* the raster data is pre-multiplied by the alpha component 
                   after invoking TIFFReadRGBAImageOriented*/
                _hasPremultipliedAlpha = true;

               memcpy(_data, raster, npixels*sizeof (uint32));
           }

          _TIFFfree(raster);
        }
        

        TIFFClose(tif);

        ret = true;
    } while (0);
    return ret;
#else
    CCLOG("tiff is not enabled, please enable it in ccConfig.h");
    return false;
#endif //CC_USE_TIFF
}

namespace
{
    bool testFormatForPvr2TCSupport(PVR2TexturePixelFormat /*format*/)
    {
        return true;
    }
    
    bool testFormatForPvr3TCSupport(PVR3TexturePixelFormat format)
    {
        switch (format) {
            case PVR3TexturePixelFormat::DXT1:
            case PVR3TexturePixelFormat::DXT3:
            case PVR3TexturePixelFormat::DXT5:
                return Configuration::getInstance()->supportsS3TC();
                
            case PVR3TexturePixelFormat::BGRA8888:
                return Configuration::getInstance()->supportsBGRA8888();
                
            case PVR3TexturePixelFormat::PVRTC2BPP_RGB:
            case PVR3TexturePixelFormat::PVRTC2BPP_RGBA:
            case PVR3TexturePixelFormat::PVRTC4BPP_RGB:
            case PVR3TexturePixelFormat::PVRTC4BPP_RGBA:
            case PVR3TexturePixelFormat::ETC1:
            case PVR3TexturePixelFormat::RGBA8888:
            case PVR3TexturePixelFormat::RGBA4444:
            case PVR3TexturePixelFormat::RGBA5551:
            case PVR3TexturePixelFormat::RGB565:
            case PVR3TexturePixelFormat::RGB888:
            case PVR3TexturePixelFormat::A8:
            case PVR3TexturePixelFormat::L8:
            case PVR3TexturePixelFormat::LA88:
                return true;
                
            default:
                return false;
        }
    }
}

bool Image::initWithPVRv2Data(const unsigned char * data, ssize_t dataLen)
{
    int dataLength = 0, dataOffset = 0, dataSize = 0;
    int blockSize = 0, widthBlocks = 0, heightBlocks = 0;
    int width = 0, height = 0;
    
    //Cast first sizeof(PVRTexHeader) bytes of data stream as PVRTexHeader
    const PVRv2TexHeader *header = static_cast<const PVRv2TexHeader *>(static_cast<const void*>(data));
    
    //Make sure that tag is in correct formatting
    if (memcmp(&header->pvrTag, gPVRTexIdentifier, strlen(gPVRTexIdentifier)) != 0)
    {
        return false;
    }
    
    Configuration *configuration = Configuration::getInstance();
    
    //can not detect the premultiplied alpha from pvr file, use _PVRHaveAlphaPremultiplied instead.
    _hasPremultipliedAlpha = _PVRHaveAlphaPremultiplied;
    
    unsigned int flags = CC_SWAP_INT32_LITTLE_TO_HOST(header->flags);
    PVR2TexturePixelFormat formatFlags = static_cast<PVR2TexturePixelFormat>(flags & PVR_TEXTURE_FLAG_TYPE_MASK);
    bool flipped = (flags & (unsigned int)PVR2TextureFlag::VerticalFlip) ? true : false;
    if (flipped)
    {
        CCLOG("cocos2d: WARNING: Image is flipped. Regenerate it using PVRTexTool");
    }
    
    if (! configuration->supportsNPOT() &&
        (static_cast<int>(header->width) != ccNextPOT(header->width)
            || static_cast<int>(header->height) != ccNextPOT(header->height)))
    {
        CCLOG("cocos2d: ERROR: Loading an NPOT texture (%dx%d) but is not supported on this device", header->width, header->height);
        return false;
    }
    
    if (!testFormatForPvr2TCSupport(formatFlags))
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant", (int)formatFlags);
        return false;
    }

    if (v2_pixel_formathash.find(formatFlags) == v2_pixel_formathash.end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant", (int)formatFlags);
        return false;
    }
    
    auto it = Texture2D::getPixelFormatInfoMap().find(getDevicePixelFormat(v2_pixel_formathash.at(formatFlags)));

    if (it == Texture2D::getPixelFormatInfoMap().end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%02X. Re-encode it with a OpenGL pixel format variant", (int)formatFlags);
        return false;
    }

    _renderFormat = it->first;
    int bpp = it->second.bpp;

    //Reset num of mipmaps
    _numberOfMipmaps = 0;

    //Get size of mipmap
    _width = width = CC_SWAP_INT32_LITTLE_TO_HOST(header->width);
    _height = height = CC_SWAP_INT32_LITTLE_TO_HOST(header->height);

    //Get ptr to where data starts..
    dataLength = CC_SWAP_INT32_LITTLE_TO_HOST(header->dataLength);

    //Move by size of header
    _dataLen = dataLen - sizeof(PVRv2TexHeader);
    _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
    memcpy(_data, (unsigned char*)data + sizeof(PVRv2TexHeader), _dataLen);

    // Calculate the data size for each texture level and respect the minimum number of blocks
    while (dataOffset < dataLength)
    {
        switch (formatFlags) {
            case PVR2TexturePixelFormat::PVRTC2BPP_RGBA:
                if (!Configuration::getInstance()->supportsPVRTC())
                {
                    CCLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                    _unpack = true;
                    _mipmaps[_numberOfMipmaps].len = width*height*4;
                    _mipmaps[_numberOfMipmaps].address = new (std::nothrow) unsigned char[width*height*4];
                    PVRTDecompressPVRTC(_data+dataOffset,width,height,_mipmaps[_numberOfMipmaps].address, true);
                    bpp = 2;
                }
                blockSize = 8 * 4; // Pixel by pixel block size for 2bpp
                widthBlocks = width / 8;
                heightBlocks = height / 4;
                break;
            case PVR2TexturePixelFormat::PVRTC4BPP_RGBA:
                if (!Configuration::getInstance()->supportsPVRTC())
                {
                    CCLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                    _unpack = true;
                    _mipmaps[_numberOfMipmaps].len = width*height*4;
                    _mipmaps[_numberOfMipmaps].address = new (std::nothrow) unsigned char[width*height*4];
                    PVRTDecompressPVRTC(_data+dataOffset,width,height,_mipmaps[_numberOfMipmaps].address, false);
                    bpp = 4;
                }
                blockSize = 4 * 4; // Pixel by pixel block size for 4bpp
                widthBlocks = width / 4;
                heightBlocks = height / 4;
                break;
            case PVR2TexturePixelFormat::BGRA8888:
                if (Configuration::getInstance()->supportsBGRA8888() == false)
                {
                    CCLOG("cocos2d: Image. BGRA8888 not supported on this device");
                    return false;
                }
            default:
                blockSize = 1;
                widthBlocks = width;
                heightBlocks = height;
                break;
        }
        
        // Clamp to minimum number of blocks
        if (widthBlocks < 2)
        {
            widthBlocks = 2;
        }
        if (heightBlocks < 2)
        {
            heightBlocks = 2;
        }
        
        dataSize = widthBlocks * heightBlocks * ((blockSize  * bpp) / 8);
        int packetLength = (dataLength - dataOffset);
        packetLength = packetLength > dataSize ? dataSize : packetLength;
        
        //Make record to the mipmaps array and increment counter
        if(!_unpack)
        {
            _mipmaps[_numberOfMipmaps].address = _data + dataOffset;
            _mipmaps[_numberOfMipmaps].len = packetLength;
        }
        _numberOfMipmaps++;
        
        dataOffset += packetLength;
        
        //Update width and height to the next lower power of two
        width = MAX(width >> 1, 1);
        height = MAX(height >> 1, 1);
    }
    
    if(_unpack)
    {
        _data = _mipmaps[0].address;
        _dataLen = _mipmaps[0].len;
    }

    return true;
}

bool Image::initWithPVRv3Data(const unsigned char * data, ssize_t dataLen)
{
    if (static_cast<size_t>(dataLen) < sizeof(PVRv3TexHeader))
    {
        return false;
    }
    
    const PVRv3TexHeader *header = static_cast<const PVRv3TexHeader *>(static_cast<const void*>(data));
    
    // validate version
    if (CC_SWAP_INT32_BIG_TO_HOST(header->version) != 0x50565203)
    {
        CCLOG("cocos2d: WARNING: pvr file version mismatch");
        return false;
    }
    
    // parse pixel format
    PVR3TexturePixelFormat pixelFormat = static_cast<PVR3TexturePixelFormat>(header->pixelFormat);
    
    if (!testFormatForPvr3TCSupport(pixelFormat))
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format variant",
              static_cast<unsigned long long>(pixelFormat));
        return false;
    }


    if (v3_pixel_formathash.find(pixelFormat) == v3_pixel_formathash.end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format variant",
              static_cast<unsigned long long>(pixelFormat));
        return false;
    }

    auto it = Texture2D::getPixelFormatInfoMap().find(getDevicePixelFormat(v3_pixel_formathash.at(pixelFormat)));

    if (it == Texture2D::getPixelFormatInfoMap().end())
    {
        CCLOG("cocos2d: WARNING: Unsupported PVR Pixel Format: 0x%016llX. Re-encode it with a OpenGL pixel format variant",
              static_cast<unsigned long long>(pixelFormat));
        return false;
    }

    _renderFormat = it->first;
    int bpp = it->second.bpp;
    
    // flags
    int flags = CC_SWAP_INT32_LITTLE_TO_HOST(header->flags);

    // PVRv3 specifies premultiply alpha in a flag -- should always respect this in PVRv3 files
    if (flags & (unsigned int)PVR3TextureFlag::PremultipliedAlpha)
    {
        _hasPremultipliedAlpha = true;
    }
    
    // sizing
    int width = CC_SWAP_INT32_LITTLE_TO_HOST(header->width);
    int height = CC_SWAP_INT32_LITTLE_TO_HOST(header->height);
    _width = width;
    _height = height;
    int dataOffset = 0, dataSize = 0;
    int blockSize = 0, widthBlocks = 0, heightBlocks = 0;
    
    _dataLen = dataLen - (sizeof(PVRv3TexHeader) + header->metadataLength);
    _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
    memcpy(_data, static_cast<const unsigned char*>(data) + sizeof(PVRv3TexHeader) + header->metadataLength, _dataLen);
    
    _numberOfMipmaps = header->numberOfMipmaps;
    CCASSERT(_numberOfMipmaps < MIPMAP_MAX, "Image: Maximum number of mimpaps reached. Increase the CC_MIPMAP_MAX value");
    
    for (int i = 0; i < _numberOfMipmaps; i++)
    {
        switch ((PVR3TexturePixelFormat)pixelFormat)
        {
            case PVR3TexturePixelFormat::PVRTC2BPP_RGB :
            case PVR3TexturePixelFormat::PVRTC2BPP_RGBA :
                if (!Configuration::getInstance()->supportsPVRTC())
                {
                    CCLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                    _unpack = true;
                    _mipmaps[i].len = width*height*4;
                    _mipmaps[i].address = new (std::nothrow) unsigned char[width*height*4];
                    PVRTDecompressPVRTC(_data+dataOffset,width,height,_mipmaps[i].address, true);
                    bpp = 2;
                }
                blockSize = 8 * 4; // Pixel by pixel block size for 2bpp
                widthBlocks = width / 8;
                heightBlocks = height / 4;
                break;
            case PVR3TexturePixelFormat::PVRTC4BPP_RGB :
            case PVR3TexturePixelFormat::PVRTC4BPP_RGBA :
                if (!Configuration::getInstance()->supportsPVRTC())
                {
                    CCLOG("cocos2d: Hardware PVR decoder not present. Using software decoder");
                    _unpack = true;
                    _mipmaps[i].len = width*height*4;
                    _mipmaps[i].address = new (std::nothrow) unsigned char[width*height*4];
                    PVRTDecompressPVRTC(_data+dataOffset,width,height,_mipmaps[i].address, false);
                    bpp = 4;
                }
                blockSize = 4 * 4; // Pixel by pixel block size for 4bpp
                widthBlocks = width / 4;
                heightBlocks = height / 4;
                break;
            case PVR3TexturePixelFormat::ETC1:
                if (!Configuration::getInstance()->supportsETC())
                {
                    CCLOG("cocos2d: Hardware ETC1 decoder not present. Using software decoder");
                    int bytePerPixel = 3;
                    unsigned int stride = width * bytePerPixel;
                    _unpack = true;
                    _mipmaps[i].len = width*height*bytePerPixel;
                    _mipmaps[i].address = new (std::nothrow) unsigned char[width*height*bytePerPixel];
                    if (etc1_decode_image(static_cast<const unsigned char*>(_data+dataOffset), static_cast<etc1_byte*>(_mipmaps[i].address), width, height, bytePerPixel, stride) != 0)
                    {
                        return false;
                    }
                }
                blockSize = 4 * 4; // Pixel by pixel block size for 4bpp
                widthBlocks = width / 4;
                heightBlocks = height / 4;
                break;
            case PVR3TexturePixelFormat::BGRA8888:
                if (! Configuration::getInstance()->supportsBGRA8888())
                {
                    CCLOG("cocos2d: Image. BGRA8888 not supported on this device");
                    return false;
                }
            default:
                blockSize = 1;
                widthBlocks = width;
                heightBlocks = height;
                break;
        }
        
        // Clamp to minimum number of blocks
        if (widthBlocks < 2)
        {
            widthBlocks = 2;
        }
        if (heightBlocks < 2)
        {
            heightBlocks = 2;
        }
        
        dataSize = widthBlocks * heightBlocks * ((blockSize  * bpp) / 8);
        auto packetLength = _dataLen - dataOffset;
        packetLength = packetLength > dataSize ? dataSize : packetLength;
        
        if(!_unpack)
        {
            _mipmaps[i].address = _data + dataOffset;
            _mipmaps[i].len = static_cast<int>(packetLength);
        }
        
        dataOffset += packetLength;
        CCASSERT(dataOffset <= _dataLen, "Image: Invalid length");
        
        
        width = MAX(width >> 1, 1);
        height = MAX(height >> 1, 1);
    }
    
    if (_unpack)
    {
        _data = _mipmaps[0].address;
        _dataLen = _mipmaps[0].len;
    }
    
    return true;
}

bool Image::initWithETCData(const unsigned char * data, ssize_t dataLen)
{
    const etc1_byte* header = static_cast<const etc1_byte*>(data);
    
    //check the data
    if (! etc1_pkm_is_valid(header))
    {
        return  false;
    }

    _width = etc1_pkm_get_width(header);
    _height = etc1_pkm_get_height(header);

    if (0 == _width || 0 == _height)
    {
        return false;
    }

    if (Configuration::getInstance()->supportsETC())
    {
        //old opengl version has no define for GL_ETC1_RGB8_OES, add macro to make compiler happy. 
#ifdef GL_ETC1_RGB8_OES
        _renderFormat = Texture2D::PixelFormat::ETC;
        _dataLen = dataLen - ETC_PKM_HEADER_SIZE;
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        memcpy(_data, static_cast<const unsigned char*>(data) + ETC_PKM_HEADER_SIZE, _dataLen);
        return true;
#else
        CC_UNUSED_PARAM(dataLen);
#endif
    }
    else
    {
        CCLOG("cocos2d: Hardware ETC1 decoder not present. Using software decoder");

         //if it is not gles or device do not support ETC, decode texture by software
        int bytePerPixel = 3;
        unsigned int stride = _width * bytePerPixel;
        _renderFormat = Texture2D::PixelFormat::RGB888;
        
        _dataLen =  _width * _height * bytePerPixel;
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        
        if (etc1_decode_image(static_cast<const unsigned char*>(data) + ETC_PKM_HEADER_SIZE, static_cast<etc1_byte*>(_data), _width, _height, bytePerPixel, stride) != 0)
        {
            _dataLen = 0;
            if (_data != nullptr)
            {
                free(_data);
            }
            return false;
        }
        
        return true;
    }
    return false;
}

bool Image::initWithTGAData(tImageTGA* tgaData)
{
    bool ret = false;
    
    do
    {
        CC_BREAK_IF(tgaData == nullptr);
        
        // tgaLoadBuffer only support type 2, 3, 10
        if (2 == tgaData->type || 10 == tgaData->type)
        {
            // true color
            // unsupported RGB555
            if (tgaData->pixelDepth == 16)
            {
                _renderFormat = Texture2D::PixelFormat::RGB5A1;
            }
            else if(tgaData->pixelDepth == 24)
            {
                _renderFormat = Texture2D::PixelFormat::RGB888;
            }
            else if(tgaData->pixelDepth == 32)
            {
                _renderFormat = Texture2D::PixelFormat::RGBA8888;
            }
            else
            {
                CCLOG("Image WARNING: unsupported true color tga data pixel format. FILE: %s", _filePath.c_str());
                break;
            }
        }
        else if(3 == tgaData->type)
        {
            // gray
            if (8 == tgaData->pixelDepth)
            {
                _renderFormat = Texture2D::PixelFormat::I8;
            }
            else
            {
                // actually this won't happen, if it happens, maybe the image file is not a tga
                CCLOG("Image WARNING: unsupported gray tga data pixel format. FILE: %s", _filePath.c_str());
                break;
            }
        }
        
        _width = tgaData->width;
        _height = tgaData->height;
        _data = tgaData->imageData;
        _dataLen = _width * _height * tgaData->pixelDepth / 8;
        _fileType = Format::TGA;

        ret = true;
        
    }while(false);
    
    if (ret)
    {
        if (FileUtils::getInstance()->getFileExtension(_filePath) != ".tga")
        {
                    CCLOG("Image WARNING: the image file suffix is not tga, but parsed as a tga image file. FILE: %s", _filePath.c_str());
        }
    }
    else
    {
        if (tgaData && tgaData->imageData != nullptr)
        {
            free(tgaData->imageData);
            _data = nullptr;
        }
    }
    
    return ret;
}

namespace
{
    static uint32_t makeFourCC(char ch0, char ch1, char ch2, char ch3)
    {
        const uint32_t fourCC = ((uint32_t)(char)(ch0) | ((uint32_t)(char)(ch1) << 8) | ((uint32_t)(char)(ch2) << 16) | ((uint32_t)(char)(ch3) << 24 ));
        return fourCC;
    }
}

bool Image::initWithS3TCData(const unsigned char * data, ssize_t dataLen)
{
    const uint32_t FOURCC_DXT1 = makeFourCC('D', 'X', 'T', '1');
    const uint32_t FOURCC_DXT3 = makeFourCC('D', 'X', 'T', '3');
    const uint32_t FOURCC_DXT5 = makeFourCC('D', 'X', 'T', '5');
    
    /* load the .dds file */
    
    S3TCTexHeader *header = (S3TCTexHeader *)data;
    unsigned char *pixelData = static_cast<unsigned char*>(malloc((dataLen - sizeof(S3TCTexHeader)) * sizeof(unsigned char)));
    memcpy((void *)pixelData, data + sizeof(S3TCTexHeader), dataLen - sizeof(S3TCTexHeader));
    
    _width = header->ddsd.width;
    _height = header->ddsd.height;
    _numberOfMipmaps = MAX(1, header->ddsd.DUMMYUNIONNAMEN2.mipMapCount); //if dds header reports 0 mipmaps, set to 1 to force correct software decoding (if needed).
    _dataLen = 0;
    int blockSize = (FOURCC_DXT1 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC) ? 8 : 16;
    
    /* calculate the dataLen */
    
    int width = _width;
    int height = _height;
    
    if (Configuration::getInstance()->supportsS3TC())  //compressed data length
    {
        _dataLen = dataLen - sizeof(S3TCTexHeader);
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        memcpy((void *)_data,(void *)pixelData , _dataLen);
    }
    else                                               //decompressed data length
    {
        for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)
        {
            if (width == 0) width = 1;
            if (height == 0) height = 1;
            
            _dataLen += (height * width *4);

            width >>= 1;
            height >>= 1;
        }
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
    }
    
    /* if hardware supports s3tc, set pixelformat before loading mipmaps, to support non-mipmapped textures  */
    if (Configuration::getInstance()->supportsS3TC())
    {   //decode texture through hardware
        
        if (FOURCC_DXT1 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
        {
            _renderFormat = Texture2D::PixelFormat::S3TC_DXT1;
        }
        else if (FOURCC_DXT3 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
        {
            _renderFormat = Texture2D::PixelFormat::S3TC_DXT3;
        }
        else if (FOURCC_DXT5 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
        {
            _renderFormat = Texture2D::PixelFormat::S3TC_DXT5;
        }
    } else { //will software decode
        _renderFormat = Texture2D::PixelFormat::RGBA8888;
    }
    
    /* load the mipmaps */
    
    int encodeOffset = 0;
    int decodeOffset = 0;
    width = _width;  height = _height;
    
    for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)  
    {
        if (width == 0) width = 1;
        if (height == 0) height = 1;
        
        int size = ((width+3)/4)*((height+3)/4)*blockSize;
                
        if (Configuration::getInstance()->supportsS3TC())
        {   //decode texture through hardware
            _mipmaps[i].address = (unsigned char *)_data + encodeOffset;
            _mipmaps[i].len = size;
        }
        else
        {   //if it is not gles or device do not support S3TC, decode texture by software
            
            CCLOG("cocos2d: Hardware S3TC decoder not present. Using software decoder");

            int bytePerPixel = 4;
            unsigned int stride = width * bytePerPixel;

            std::vector<unsigned char> decodeImageData(stride * height);
            if (FOURCC_DXT1 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
            {
                s3tc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, S3TCDecodeFlag::DXT1);
            }
            else if (FOURCC_DXT3 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
            {
                s3tc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, S3TCDecodeFlag::DXT3);
            }
            else if (FOURCC_DXT5 == header->ddsd.DUMMYUNIONNAMEN4.ddpfPixelFormat.fourCC)
            {
                s3tc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, S3TCDecodeFlag::DXT5);
            }
            
            _mipmaps[i].address = (unsigned char *)_data + decodeOffset;
            _mipmaps[i].len = (stride * height);
            memcpy((void *)_mipmaps[i].address, (void *)&decodeImageData[0], _mipmaps[i].len);
            decodeOffset += stride * height;
        }
        
        encodeOffset += size;
        width >>= 1;
        height >>= 1;
    }
    
    /* end load the mipmaps */
    
    if (pixelData != nullptr)
    {
        free(pixelData);
    };
    
    return true;
}


bool Image::initWithATITCData(const unsigned char *data, ssize_t dataLen)
{
    /* load the .ktx file */
    ATITCTexHeader *header = (ATITCTexHeader *)data;
    _width =  header->pixelWidth;
    _height = header->pixelHeight;
    _numberOfMipmaps = header->numberOfMipmapLevels;
    
    int blockSize = 0;
    switch (header->glInternalFormat)
    {
        case CC_GL_ATC_RGB_AMD:
            blockSize = 8;
            break;
        case CC_GL_ATC_RGBA_EXPLICIT_ALPHA_AMD:
            blockSize = 16;
            break;
        case CC_GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD:
            blockSize = 16;
            break;
        default:
            break;
    }
    
    /* pixelData point to the compressed data address */
    unsigned char *pixelData = (unsigned char *)data + sizeof(ATITCTexHeader) + header->bytesOfKeyValueData + 4;
    
    /* calculate the dataLen */
    int width = _width;
    int height = _height;
    
    if (Configuration::getInstance()->supportsATITC())  //compressed data length
    {
        _dataLen = dataLen - sizeof(ATITCTexHeader) - header->bytesOfKeyValueData - 4;
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        memcpy((void *)_data,(void *)pixelData , _dataLen);
    }
    else                                               //decompressed data length
    {
        for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)
        {
            if (width == 0) width = 1;
            if (height == 0) height = 1;
            
            _dataLen += (height * width *4);
            
            width >>= 1;
            height >>= 1;
        }
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
    }
    
    /* load the mipmaps */
    int encodeOffset = 0;
    int decodeOffset = 0;
    width = _width;  height = _height;
    
    for (int i = 0; i < _numberOfMipmaps && (width || height); ++i)
    {
        if (width == 0) width = 1;
        if (height == 0) height = 1;
        
        int size = ((width+3)/4)*((height+3)/4)*blockSize;
        
        if (Configuration::getInstance()->supportsATITC())
        {
            /* decode texture through hardware */
            
            CCLOG("this is atitc H decode");
            
            switch (header->glInternalFormat)
            {
                case CC_GL_ATC_RGB_AMD:
                    _renderFormat = Texture2D::PixelFormat::ATC_RGB;
                    break;
                case CC_GL_ATC_RGBA_EXPLICIT_ALPHA_AMD:
                    _renderFormat = Texture2D::PixelFormat::ATC_EXPLICIT_ALPHA;
                    break;
                case CC_GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD:
                    _renderFormat = Texture2D::PixelFormat::ATC_INTERPOLATED_ALPHA;
                    break;
                default:
                    break;
            }
            
            _mipmaps[i].address = (unsigned char *)_data + encodeOffset;
            _mipmaps[i].len = size;
        }
        else
        {
            /* if it is not gles or device do not support ATITC, decode texture by software */
            
            CCLOG("cocos2d: Hardware ATITC decoder not present. Using software decoder");
            
            int bytePerPixel = 4;
            unsigned int stride = width * bytePerPixel;
            _renderFormat = Texture2D::PixelFormat::RGBA8888;
            
            std::vector<unsigned char> decodeImageData(stride * height);
            switch (header->glInternalFormat)
            {
                case CC_GL_ATC_RGB_AMD:
                    atitc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, ATITCDecodeFlag::ATC_RGB);
                    break;
                case CC_GL_ATC_RGBA_EXPLICIT_ALPHA_AMD:
                    atitc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, ATITCDecodeFlag::ATC_EXPLICIT_ALPHA);
                    break;
                case CC_GL_ATC_RGBA_INTERPOLATED_ALPHA_AMD:
                    atitc_decode(pixelData + encodeOffset, &decodeImageData[0], width, height, ATITCDecodeFlag::ATC_INTERPOLATED_ALPHA);
                    break;
                default:
                    break;
            }

            _mipmaps[i].address = (unsigned char *)_data + decodeOffset;
            _mipmaps[i].len = (stride * height);
            memcpy((void *)_mipmaps[i].address, (void *)&decodeImageData[0], _mipmaps[i].len);
            decodeOffset += stride * height;
        }

        encodeOffset += (size + 4);
        width >>= 1;
        height >>= 1;
    }
    /* end load the mipmaps */
    
    return true;
}

bool Image::initWithPVRData(const unsigned char * data, ssize_t dataLen)
{
    return initWithPVRv2Data(data, dataLen) || initWithPVRv3Data(data, dataLen);
}

bool Image::initWithWebpData(const unsigned char * data, ssize_t dataLen)
{
#if CC_USE_WEBP
    bool ret = false;

#if (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
    CCLOG("WEBP image format not supported on WinRT or WP8");
#else
    do
    {
        WebPDecoderConfig config;
        if (WebPInitDecoderConfig(&config) == 0) break;
        if (WebPGetFeatures(static_cast<const uint8_t*>(data), dataLen, &config.input) != VP8_STATUS_OK) break;
        if (config.input.width == 0 || config.input.height == 0) break;
        
        config.output.colorspace = config.input.has_alpha?MODE_rgbA:MODE_RGB;
        _renderFormat = config.input.has_alpha?Texture2D::PixelFormat::RGBA8888:Texture2D::PixelFormat::RGB888;
        _width    = config.input.width;
        _height   = config.input.height;
        
        //we ask webp to give data with premultiplied alpha
        _hasPremultipliedAlpha = (config.input.has_alpha != 0);
        
        _dataLen = _width * _height * (config.input.has_alpha?4:3);
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        
        config.output.u.RGBA.rgba = static_cast<uint8_t*>(_data);
        config.output.u.RGBA.stride = _width * (config.input.has_alpha?4:3);
        config.output.u.RGBA.size = _dataLen;
        config.output.is_external_memory = 1;
        
        if (WebPDecode(static_cast<const uint8_t*>(data), dataLen, &config) != VP8_STATUS_OK)
        {
            free(_data);
            _data = nullptr;
            break;
        }
        
        ret = true;
    } while (0);
#endif // (CC_TARGET_PLATFORM == CC_PLATFORM_WINRT)
    return ret;
#else 
    CCLOG("webp is not enabled, please enable it in ccConfig.h");
    return false;
#endif // CC_USE_WEBP
}


bool Image::initWithRawData(const unsigned char * data, ssize_t /*dataLen*/, int width, int height, int /*bitsPerComponent*/, bool preMulti)
{
    bool ret = false;
    do 
    {
        CC_BREAK_IF(0 == width || 0 == height);

        _height   = height;
        _width    = width;
        _hasPremultipliedAlpha = preMulti;
        _renderFormat = Texture2D::PixelFormat::RGBA8888;

        // only RGBA8888 supported
        int bytesPerComponent = 4;
        _dataLen = height * width * bytesPerComponent;
        _data = static_cast<unsigned char*>(malloc(_dataLen * sizeof(unsigned char)));
        CC_BREAK_IF(! _data);
        memcpy(_data, data, _dataLen);

        ret = true;
    } while (0);

    return ret;
}


#if (CC_TARGET_PLATFORM != CC_PLATFORM_IOS)
bool Image::saveToFile(const std::string& filename, bool isToRGB)
{
    //only support for Texture2D::PixelFormat::RGB888 or Texture2D::PixelFormat::RGBA8888 uncompressed data
    if (isCompressed() || (_renderFormat != Texture2D::PixelFormat::RGB888 && _renderFormat != Texture2D::PixelFormat::RGBA8888))
    {
        CCLOG("cocos2d: Image: saveToFile is only support for Texture2D::PixelFormat::RGB888 or Texture2D::PixelFormat::RGBA8888 uncompressed data for now");
        return false;
    }

    std::string fileExtension = FileUtils::getInstance()->getFileExtension(filename);

    if (fileExtension == ".png")
    {
        return saveImageToPNG(filename, isToRGB);
    }
    else if (fileExtension == ".jpg")
    {
        return saveImageToJPG(filename);
    }
    else
    {
        CCLOG("cocos2d: Image: saveToFile no support file extension(only .png or .jpg) for file: %s", filename.c_str());
        return false;
    }
}
#endif

bool Image::saveImageToPNG(const std::string& filePath, bool isToRGB)
{
#if CC_USE_WIC
    return encodeWithWIC(filePath, isToRGB, GUID_ContainerFormatPng);
#elif CC_USE_PNG
    bool ret = false;
    do
    {
        FILE *fp;
        png_structp png_ptr;
        png_infop info_ptr;
        png_colorp palette;
        png_bytep *row_pointers;

        fp = fopen(FileUtils::getInstance()->getSuitableFOpen(filePath).c_str(), "wb");
        CC_BREAK_IF(nullptr == fp);

        png_ptr = png_create_write_struct(PNG_LIBPNG_VER_STRING, nullptr, nullptr, nullptr);

        if (nullptr == png_ptr)
        {
            fclose(fp);
            break;
        }

        info_ptr = png_create_info_struct(png_ptr);
        if (nullptr == info_ptr)
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, nullptr);
            break;
        }
#if (CC_TARGET_PLATFORM != CC_PLATFORM_BADA && CC_TARGET_PLATFORM != CC_PLATFORM_NACL && CC_TARGET_PLATFORM != CC_PLATFORM_TIZEN)
        if (setjmp(png_jmpbuf(png_ptr)))
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }
#endif
        png_init_io(png_ptr, fp);

        if (!isToRGB && hasAlpha())
        {
            png_set_IHDR(png_ptr, info_ptr, _width, _height, 8, PNG_COLOR_TYPE_RGB_ALPHA,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        } 
        else
        {
            png_set_IHDR(png_ptr, info_ptr, _width, _height, 8, PNG_COLOR_TYPE_RGB,
                PNG_INTERLACE_NONE, PNG_COMPRESSION_TYPE_BASE, PNG_FILTER_TYPE_BASE);
        }

        palette = (png_colorp)png_malloc(png_ptr, PNG_MAX_PALETTE_LENGTH * sizeof (png_color));
        png_set_PLTE(png_ptr, info_ptr, palette, PNG_MAX_PALETTE_LENGTH);

        png_write_info(png_ptr, info_ptr);

        png_set_packing(png_ptr);

        row_pointers = (png_bytep *)malloc(_height * sizeof(png_bytep));
        if(row_pointers == nullptr)
        {
            fclose(fp);
            png_destroy_write_struct(&png_ptr, &info_ptr);
            break;
        }

        if (!hasAlpha())
        {
            for (int i = 0; i < (int)_height; i++)
            {
                row_pointers[i] = (png_bytep)_data + i * _width * 3;
            }

            png_write_image(png_ptr, row_pointers);

            free(row_pointers);
            row_pointers = nullptr;
        }
        else
        {
            if (isToRGB)
            {
                unsigned char *tempData = static_cast<unsigned char*>(malloc(_width * _height * 3 * sizeof(unsigned char)));
                if (nullptr == tempData)
                {
                    fclose(fp);
                    png_destroy_write_struct(&png_ptr, &info_ptr);
                    
                    free(row_pointers);
                    row_pointers = nullptr;
                    break;
                }

                for (int i = 0; i < _height; ++i)
                {
                    for (int j = 0; j < _width; ++j)
                    {
                        tempData[(i * _width + j) * 3] = _data[(i * _width + j) * 4];
                        tempData[(i * _width + j) * 3 + 1] = _data[(i * _width + j) * 4 + 1];
                        tempData[(i * _width + j) * 3 + 2] = _data[(i * _width + j) * 4 + 2];
                    }
                }

                for (int i = 0; i < (int)_height; i++)
                {
                    row_pointers[i] = (png_bytep)tempData + i * _width * 3;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = nullptr;

                if (tempData != nullptr)
                {
                    free(tempData);
                }
            } 
            else
            {
                for (int i = 0; i < (int)_height; i++)
                {
                    row_pointers[i] = (png_bytep)_data + i * _width * 4;
                }

                png_write_image(png_ptr, row_pointers);

                free(row_pointers);
                row_pointers = nullptr;
            }
        }

        png_write_end(png_ptr, info_ptr);

        png_free(png_ptr, palette);
        palette = nullptr;

        png_destroy_write_struct(&png_ptr, &info_ptr);

        fclose(fp);

        ret = true;
    } while (0);
    return ret;
#else
    CCLOG("png is not enabled, please enable it in ccConfig.h");
    return false;
#endif // CC_USE_PNG
}

bool Image::saveImageToJPG(const std::string& filePath)
{
#if CC_USE_WIC
    return encodeWithWIC(filePath, false, GUID_ContainerFormatJpeg);
#elif CC_USE_JPEG
    bool ret = false;
    do 
    {
        struct jpeg_compress_struct cinfo;
        struct jpeg_error_mgr jerr;
        FILE * outfile;                 /* target file */
        JSAMPROW row_pointer[1];        /* pointer to JSAMPLE row[s] */
        int     row_stride;          /* physical row width in image buffer */

        cinfo.err = jpeg_std_error(&jerr);
        /* Now we can initialize the JPEG compression object. */
        jpeg_create_compress(&cinfo);

        CC_BREAK_IF((outfile = fopen(FileUtils::getInstance()->getSuitableFOpen(filePath).c_str(), "wb")) == nullptr);
        
        jpeg_stdio_dest(&cinfo, outfile);

        cinfo.image_width = _width;    /* image width and height, in pixels */
        cinfo.image_height = _height;
        cinfo.input_components = 3;       /* # of color components per pixel */
        cinfo.in_color_space = JCS_RGB;       /* colorspace of input image */

        jpeg_set_defaults(&cinfo);
        jpeg_set_quality(&cinfo, 90, TRUE);
        
        jpeg_start_compress(&cinfo, TRUE);

        row_stride = _width * 3; /* JSAMPLEs per row in image_buffer */

        if (hasAlpha())
        {
            unsigned char *tempData = static_cast<unsigned char*>(malloc(_width * _height * 3 * sizeof(unsigned char)));
            if (nullptr == tempData)
            {
                jpeg_finish_compress(&cinfo);
                jpeg_destroy_compress(&cinfo);
                fclose(outfile);
                break;
            }

            for (int i = 0; i < _height; ++i)
            {
                for (int j = 0; j < _width; ++j)

                {
                    tempData[(i * _width + j) * 3] = _data[(i * _width + j) * 4];
                    tempData[(i * _width + j) * 3 + 1] = _data[(i * _width + j) * 4 + 1];
                    tempData[(i * _width + j) * 3 + 2] = _data[(i * _width + j) * 4 + 2];
                }
            }

            while (cinfo.next_scanline < cinfo.image_height)
            {
                row_pointer[0] = & tempData[cinfo.next_scanline * row_stride];
                (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }

            if (tempData != nullptr)
            {
                free(tempData);
            }
        } 
        else
        {
            while (cinfo.next_scanline < cinfo.image_height) {
                row_pointer[0] = & _data[cinfo.next_scanline * row_stride];
                (void) jpeg_write_scanlines(&cinfo, row_pointer, 1);
            }
        }

        jpeg_finish_compress(&cinfo);
        fclose(outfile);
        jpeg_destroy_compress(&cinfo);
        
        ret = true;
    } while (0);
    return ret;
#else
    CCLOG("jpeg is not enabled, please enable it in ccConfig.h");
    return false;
#endif // CC_USE_JPEG
}

void Image::premultipliedAlpha()
{
#if CC_ENABLE_PREMULTIPLIED_ALPHA == 0
        _hasPremultipliedAlpha = false;
        return;
#else
    CCASSERT(_renderFormat == Texture2D::PixelFormat::RGBA8888, "The pixel format should be RGBA8888!");
    
    unsigned int* fourBytes = (unsigned int*)_data;
    for(int i = 0; i < _width * _height; i++)
    {
        unsigned char* p = _data + i * 4;
        fourBytes[i] = CC_RGB_PREMULTIPLY_ALPHA(p[0], p[1], p[2], p[3]);
    }
    
    _hasPremultipliedAlpha = true;
#endif
}


void Image::setPVRImagesHavePremultipliedAlpha(bool haveAlphaPremultiplied)
{
    _PVRHaveAlphaPremultiplied = haveAlphaPremultiplied;
}

NS_CC_END

