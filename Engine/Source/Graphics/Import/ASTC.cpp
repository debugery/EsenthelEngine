/******************************************************************************/
#include "stdafx.h"

/******************************************************************************/
#include "../../../../ThirdPartyLibs/begin.h"

#define ASTC_LIB_ISPC 1

#define ASTC_ENC ASTC_LIB_ISPC

#if ASTC_ENC==ASTC_LIB_ISPC
   #if (WINDOWS && !ARM) || MAC
      #include "../../../../ThirdPartyLibs/BC/ispc_texcomp/ispc_texcomp.h" // Windows and Mac link against precompiled lib's generated by Intel Compiler, so all we need is a header
   #else // other platforms need source
      #include "../../../../ThirdPartyLibs/BC/ispc_texcomp/ispc_texcomp_astc.cpp"
      #include "../../../../ThirdPartyLibs/BC/kernel_astc.ispc.cpp"
   #endif
#endif

#include "../../../../ThirdPartyLibs/end.h"

namespace EE{
/******************************************************************************/
Bool (*CompressASTC)(C Image &src, Image &dest);
/******************************************************************************/
struct ASTCContext
{
#if ASTC_ENC==ASTC_LIB_ISPC
   astc_enc_settings settings;
#endif
 C Image *src;
   Image *dest;
   Int    thread_blocks, threads;
   VecI2  size;

   ASTCContext(Image &dest)
   {
      T.dest=&dest;
   #if ASTC_ENC==ASTC_LIB_ISPC
      #if 0 // FIXME which one?
         GetProfile_astc_alpha_slow(&settings, 4, 4); // FIXME
      #else
         GetProfile_astc_alpha_fast(&settings, 4, 4); // FIXME
      #endif
   #endif
   }
   void init(C Image &src)
   {
      T.src=&src;
      Int total_blocks=size.y/4; // FIXME
      threads  =Min(total_blocks, ImageThreads.threads1()); // +1 because we will do processing on the caller thread too
      thread_blocks=total_blocks/threads;
   }
};
/******************************************************************************/
static void CompressASTCBlock(IntPtr elm_index, ASTCContext &data, Int thread_index)
{
   Int block_start=elm_index*data.thread_blocks, y_start=block_start*4; // FIXME
#if ASTC_ENC==ASTC_LIB_ISPC
   rgba_surface surf;
   surf.ptr   =ConstCast(data.src->data()+y_start*data.src->pitch());
   surf.stride=data.src->pitch();
   surf.width =data.size.x;
   surf.height=((elm_index==data.threads-1) ? data.size.y-y_start : data.thread_blocks*4); // last thread must process all remaining blocks // FIXME
   CompressBlocksASTC(&surf, data.dest->data() + block_start*data.dest->pitch(), &data.settings);
#endif
}
/******************************************************************************/
Bool _CompressASTC(C Image &src, Image &dest)
{
   if(dest.hwType()==IMAGE_ASTC_4x4
   || dest.hwType()==IMAGE_ASTC_4x4_SRGB)
   {
      ImageThreads.init(); ASTCContext data(dest);
      Int src_faces1=src.faces()-1;
      Image temp; // define outside loop to avoid overhead
      REPD(mip, Min(src.mipMaps(), dest.mipMaps()))
      {
         data.size.set(PaddedWidth (dest.hwW(), dest.hwH(), mip, dest.hwType()), // operate on mip HW size to process partial and Pow2Padded blocks too
                       PaddedHeight(dest.hwW(), dest.hwH(), mip, dest.hwType()));
         // to directly read from 'src', we need to match requirements for compressor, which needs:
         Bool read_from_src=((src.hwType()==IMAGE_R8G8B8A8 || src.hwType()==IMAGE_R8G8B8A8_SRGB)
                         && Max(1, src.w()>>mip)>=data.size.x   // src mip valid width  must be at least as dest mip width
                         && Max(1, src.h()>>mip)>=data.size.y); // src mip valid height must be at least as dest mip height
       C Image &s=(read_from_src ? src : temp);
         REPD(face, dest.faces())
         {
            if(!read_from_src)
            {
               if(!src.extractNonCompressedMipMapNoStretch(temp, data.size.x, data.size.y, 1, mip, (DIR_ENUM)Min(face, src_faces1), true))return false;
               {if(temp.hwType()!=IMAGE_R8G8B8A8 && temp.hwType()!=IMAGE_R8G8B8A8_SRGB)if(!temp.copyTry(temp, -1, -1, -1, dest.sRGB() ? IMAGE_R8G8B8A8_SRGB : IMAGE_R8G8B8A8))return false;}
            }else
            if(! src.lockRead(            mip, (DIR_ENUM)Min(face, src_faces1)))                                return false; // we have to lock only for 'src' because 'temp' is 1mip-1face-SOFT and doesn't need locking
            if(!dest.lock    (LOCK_WRITE, mip, (DIR_ENUM)    face             )){if(read_from_src)src.unlock(); return false;}

            data.init(s); // !! call after 'ImageThreads.init' !!
            ImageThreads.process(data.threads, CompressASTCBlock, data);

                            dest.unlock();
            if(read_from_src)src.unlock();
         }
      }
      return true;
   }
   return false;
}
/******************************************************************************/
}
/******************************************************************************/