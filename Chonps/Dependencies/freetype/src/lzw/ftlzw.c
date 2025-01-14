/****************************************************************************
 *
 * ftlzw.c
 *
 *   FreeType support for .Z compressed files.
 *
 * This optional component relies on NetBSD's zopen().  It should mainly
 * be used to parse compressed PCF fonts, as found with many X11 server
 * distributions.
 *
 * Copyright (C) 2004-2023 by
 * Albert Chin-A-Young.
 *
 * based on code in `src/gzip/ftgzip.c'
 *
 * This file is part of the FreeType project, and may only be used,
 * modified, and distributed under the terms of the FreeType project
 * license, LICENSE.TXT.  By continuing to use, modify, or distribute
 * this file you indicate that you have read the license and
 * understand and accept it fully.
 *
 */

#include <freetype/internal/ftmemory.h>
#include <freetype/internal/ftstream.h>
#include <freetype/internal/ftdebug.h>
#include <freetype/ftlzw.h>
#include FT_CONFIG_STANDARD_LIBRARY_H


#include <freetype/ftmoderr.h>

#undef FTERRORS_H_

#undef  FT_ERR_PREFIX
#define FT_ERR_PREFIX  LZW_Err_
#define FT_ERR_BASE    FT_Mod_Err_LZW

#include <freetype/fterrors.h>


#ifdef FT_CONFIG_OPTION_USE_LZW

#include "ftzopen.h"


/***************************************************************************/
/***************************************************************************/
/*****                                                                 *****/
/*****                  M E M O R Y   M A N A G E M E N T              *****/
/*****                                                                 *****/
/***************************************************************************/
/***************************************************************************/

/***************************************************************************/
/***************************************************************************/
/*****                                                                 *****/
/*****                   F I L E   D E S C R I P T O R                 *****/
/*****                                                                 *****/
/***************************************************************************/
/***************************************************************************/

#define FT_LZW_BUFFER_SIZE  4096

  typedef struct  FT_LZWFileRec_
  {
    FT_Stream       source;         /* parent/source stream        */
    FT_Stream       stream;         /* embedding stream            */
    FT_Memory       memory;         /* memory allocator            */
    FT_LzwStateRec  lzw;            /* lzw decompressor state      */

    FT_Byte         buffer[FT_LZW_BUFFER_SIZE]; /* output buffer      */
    FT_ULong        pos;                        /* position in output */
    FT_Byte*        cursor;
    FT_Byte*        limit;

  } FT_LZWFileRec, *FT_LZWFile;


  /* check and skip .Z header */
  static FT_Error
  ft_lzw_check_header( FT_Stream  stream )
  {
    FT_Error  error;
    FT_Byte   head[2];


    if ( FT_STREAM_SEEK( 0 )       ||
         FT_STREAM_READ( head, 2 ) )
      goto Exit;

    /* head[0] && head[1] are the magic numbers */
    if ( head[0] != 0x1F ||
         head[1] != 0x9D )
      error = FT_THROW( Invalid_File_Format );

  Exit:
    return error;
  }


  static FT_Error
  ft_lzw_file_init( FT_LZWFile  zip,
                    FT_Stream   stream,
                    FT_Stream   source )
  {
    FT_LzwState  lzw   = &zip->lzw;
    FT_Error     error;


    zip->stream = stream;
    zip->source = source;
    zip->memory = stream->memory;

    zip->limit  = zip->buffer + FT_LZW_BUFFER_SIZE;
    zip->cursor = zip->limit;
    zip->pos    = 0;

    /* check and skip .Z header */
    error = ft_lzw_check_header( source );
    if ( error )
      goto Exit;

    /* initialize internal lzw variable */
    ft_lzwstate_init( lzw, source );

  Exit:
    return error;
  }


  static void
  ft_lzw_file_done( FT_LZWFile  zip )
  {
    /* clear the rest */
    ft_lzwstate_done( &zip->lzw );

    zip->memory = NULL;
    zip->source = NULL;
    zip->stream = NULL;
  }


  static FT_Error
  ft_lzw_file_reset( FT_LZWFile  zip )
  {
    FT_Stream  stream = zip->source;
    FT_Error   error;


    if ( !FT_STREAM_SEEK( 0 ) )
    {
      ft_lzwstate_reset( &zip->lzw );

      zip->limit  = zip->buffer + FT_LZW_BUFFER_SIZE;
      zip->cursor = zip->limit;
      zip->pos    = 0;
    }

    return error;
  }


  static FT_Error
  ft_lzw_file_fill_output( FT_LZWFile  zip )
  {
    FT_LzwState  lzw = &zip->lzw;
    FT_ULong     count;
    FT_Error     error = FT_Err_Ok;


    zip->cursor = zip->buffer;

    count = ft_lzwstate_io( lzw, zip->buffer, FT_LZW_BUFFER_SIZE );

    zip->limit = zip->cursor + count;

    if ( count == 0 )
      error = FT_THROW( Invalid_Stream_Operation );

    return error;
  }


  /* fill output buffer; `count' must be <= FT_LZW_BUFFER_SIZE */
  static FT_Error
  ft_lzw_file_skip_output( FT_LZWFile  zip,
                           FT_ULong    count )
  {
    FT_Error  error = FT_Err_Ok;


    /* first, we skip what we can from the output buffer */
    {
      FT_ULong  delta = (FT_ULong)( zip->limit - zip->cursor );


      if ( delta >= count )
        delta = count;

      zip->cursor += delta;
      zip->pos    += delta;

      count -= delta;
    }

    /* next, we skip as many bytes remaining as possible */
    while ( count > 0 )
    {
      FT_ULong  delta = FT_LZW_BUFFER_SIZE;
      FT_ULong  numread;


      if ( delta > count )
        delta = count;

      numread = ft_lzwstate_io( &zip->lzw, NULL, delta );
      if ( numread < delta )
      {
        /* not enough bytes */
        error = FT_THROW( Invalid_Stream_Operation );
        break;
      }

      zip->pos += delta;
      count    -= delta;
    }

    return error;
  }


  static FT_ULong
  ft_lzw_file_io( FT_LZWFile  zip,
                  FT_ULong    pos,
                  FT_Byte*    buffer,
                  FT_ULong    count )
  {
    FT_ULong  result = 0;
    FT_Error  error;


    /* seeking backwards. */
    if ( pos < zip->pos )
    {
      /* If the new position is within the output buffer, simply       */
      /* decrement pointers, otherwise we reset the stream completely! */
      if ( ( zip->pos - pos ) <= (FT_ULong)( zip->cursor - zip->buffer ) )
      {
        zip->cursor -= zip->pos - pos;
        zip->pos     = pos;
      }
      else
      {
        error = ft_lzw_file_reset( zip );
        if ( error )
          goto Exit;
      }
    }

    /* skip unwanted bytes */
    if ( pos > zip->pos )
    {
      error = ft_lzw_file_skip_output( zip, (FT_ULong)( pos - zip->pos ) );
      if ( error )
        goto Exit;
    }

    if ( count == 0 )
      goto Exit;

    /* now read the data */
    for (;;)
    {
      FT_ULong  delta;


      delta = (FT_ULong)( zip->limit - zip->cursor );
      if ( delta >= count )
        delta = count;

      FT_MEM_COPY( buffer + result, zip->cursor, delta );
      result      += delta;
      zip->cursor += delta;
      zip->pos    += delta;

      count -= delta;
      if ( count == 0 )
        break;

      error = ft_lzw_file_fill_output( zip );
      if ( error )
        break;
    }

  Exit:
    return result;
  }


/***************************************************************************/
/***************************************************************************/
/*****                                                                 *****/
/*****            L Z W   E M B E D D I N G   S T R E A M              *****/
/*****                                                                 *****/
/***************************************************************************/
/***************************************************************************/

  static void
  ft_lzw_stream_close( FT_Stream  stream )
  {
    FT_LZWFile  zip    = (FT_LZWFile)stream->descriptor.pointer;
    FT_Memory   memory = stream->memory;


    if ( zip )
    {
      /* finalize lzw file descriptor */
      ft_lzw_file_done( zip );

      FT_FREE( zip );

      stream->descriptor.pointer = NULL;
    }
  }


  static unsigned long
  ft_lzw_stream_io( FT_Stream       stream,
                    unsigned long   offset,
                    unsigned char*  buffer,
                    unsigned long   count )
  {
    FT_LZWFile  zip = (FT_LZWFile)stream->descriptor.pointer;


    return ft_lzw_file_io( zip, offset, buffer, count );
  }


  FT_EXPORT_DEF( FT_Error )
  FT_Stream_OpenLZW( FT_Stream  stream,
                     FT_Stream  source )
  {
    FT_Error    error;
    FT_Memory   memory;
    FT_LZWFile  zip = NULL;


    if ( !stream || !source )
    {
      error = FT_THROW( Invalid_Stream_Handle );
      goto Exit;
    }

    memory = source->memory;

    /*
     * Check the header right now; this prevents allocation of a huge
     * LZWFile object (400 KByte of heap memory) if not necessary.
     *
     * Did I mention that you should never use .Z compressed font
     * files?
     */
    error = ft_lzw_check_header( source );
    if ( error )
      goto Exit;

    FT_ZERO( stream );
    stream->memory = memory;

    if ( !FT_QNEW( zip ) )
    {
      error = ft_lzw_file_init( zip, stream, source );
      if ( error )
      {
        FT_FREE( zip );
        goto Exit;
      }

      stream->descriptor.pointer = zip;
    }

    stream->size  = 0x7FFFFFFFL;  /* don't know the real size! */
    stream->pos   = 0;
    stream->base  = NULL;
    stream->read  = ft_lzw_stream_io;
    stream->close = ft_lzw_stream_close;

  Exit:
    return error;
  }


#include <freetype/internal/ftmemory.h>
#include <freetype/internal/ftstream.h>
#include <freetype/internal/ftdebug.h>


  static int
      ft_lzwstate_refill(FT_LzwState  state)
  {
      FT_ULong  count;


      if (state->in_eof)
          return -1;

      count = FT_Stream_TryRead(state->source,
          state->buf_tab,
          state->num_bits);  /* WHY? */

      state->buf_size = (FT_UInt)count;
      state->buf_total += count;
      state->in_eof = FT_BOOL(count < state->num_bits);
      state->buf_offset = 0;

      state->buf_size <<= 3;
      if (state->buf_size > state->num_bits)
          state->buf_size -= state->num_bits - 1;
      else
          return -1; /* not enough data */

      if (count == 0)  /* end of file */
          return -1;

      return 0;
  }


  static FT_Int32
      ft_lzwstate_get_code(FT_LzwState  state)
  {
      FT_UInt   num_bits = state->num_bits;
      FT_UInt   offset = state->buf_offset;
      FT_Byte* p;
      FT_Int    result;


      if (state->buf_clear ||
          offset >= state->buf_size ||
          state->free_ent >= state->free_bits)
      {
          if (state->free_ent >= state->free_bits)
          {
              state->num_bits = ++num_bits;
              if (num_bits > LZW_MAX_BITS)
                  return -1;

              state->free_bits = state->num_bits < state->max_bits
                  ? (FT_UInt)((1UL << num_bits) - 256)
                  : state->max_free + 1;
          }

          if (state->buf_clear)
          {
              state->num_bits = num_bits = LZW_INIT_BITS;
              state->free_bits = (FT_UInt)((1UL << num_bits) - 256);
              state->buf_clear = 0;
          }

          if (ft_lzwstate_refill(state) < 0)
              return -1;

          offset = 0;
      }

      state->buf_offset = offset + num_bits;

      p = &state->buf_tab[offset >> 3];
      offset &= 7;
      result = *p++ >> offset;
      offset = 8 - offset;
      num_bits -= offset;

      if (num_bits >= 8)
      {
          result |= *p++ << offset;
          offset += 8;
          num_bits -= 8;
      }
      if (num_bits > 0)
          result |= (*p & LZW_MASK(num_bits)) << offset;

      return result;
  }


  /* grow the character stack */
  static int
      ft_lzwstate_stack_grow(FT_LzwState  state)
  {
      if (state->stack_top >= state->stack_size)
      {
          FT_Memory  memory = state->memory;
          FT_Error   error;
          FT_Offset  old_size = state->stack_size;
          FT_Offset  new_size = old_size;

          new_size = new_size + (new_size >> 1) + 4;

          /* if relocating to heap */
          if (state->stack == state->stack_0)
          {
              state->stack = NULL;
              old_size = 0;
          }

          /* requirement of the character stack larger than 1<<LZW_MAX_BITS */
          /* implies bug in the decompression code                          */
          if (new_size > (1 << LZW_MAX_BITS))
          {
              new_size = 1 << LZW_MAX_BITS;
              if (new_size == old_size)
                  return -1;
          }

          if (FT_QREALLOC(state->stack, old_size, new_size))
              return -1;

          /* if relocating to heap */
          if (old_size == 0)
              FT_MEM_COPY(state->stack, state->stack_0, FT_LZW_DEFAULT_STACK_SIZE);

          state->stack_size = new_size;
      }
      return 0;
  }


  /* grow the prefix/suffix arrays */
  static int
      ft_lzwstate_prefix_grow(FT_LzwState  state)
  {
      FT_UInt    old_size = state->prefix_size;
      FT_UInt    new_size = old_size;
      FT_Memory  memory = state->memory;
      FT_Error   error;


      if (new_size == 0)  /* first allocation -> 9 bits */
          new_size = 512;
      else
          new_size += new_size >> 2;  /* don't grow too fast */

        /*
         * Note that the `suffix' array is located in the same memory block
         * pointed to by `prefix'.
         *
         * I know that sizeof(FT_Byte) == 1 by definition, but it is clearer
         * to write it literally.
         *
         */
      if (FT_REALLOC_MULT(state->prefix, old_size, new_size,
          sizeof(FT_UShort) + sizeof(FT_Byte)))
          return -1;

      /* now adjust `suffix' and move the data accordingly */
      state->suffix = (FT_Byte*)(state->prefix + new_size);

      FT_MEM_MOVE(state->suffix,
          state->prefix + old_size,
          old_size * sizeof(FT_Byte));

      state->prefix_size = new_size;
      return 0;
  }


  FT_LOCAL_DEF(void)
      ft_lzwstate_reset(FT_LzwState  state)
  {
      state->in_eof = 0;
      state->buf_offset = 0;
      state->buf_size = 0;
      state->buf_clear = 0;
      state->buf_total = 0;
      state->stack_top = 0;
      state->num_bits = LZW_INIT_BITS;
      state->phase = FT_LZW_PHASE_START;
  }


  FT_LOCAL_DEF(void)
      ft_lzwstate_init(FT_LzwState  state,
          FT_Stream    source)
  {
      FT_ZERO(state);

      state->source = source;
      state->memory = source->memory;

      state->prefix = NULL;
      state->suffix = NULL;
      state->prefix_size = 0;

      state->stack = state->stack_0;
      state->stack_size = sizeof(state->stack_0);

      ft_lzwstate_reset(state);
  }


  FT_LOCAL_DEF(void)
      ft_lzwstate_done(FT_LzwState  state)
  {
      FT_Memory  memory = state->memory;


      ft_lzwstate_reset(state);

      if (state->stack != state->stack_0)
          FT_FREE(state->stack);

      FT_FREE(state->prefix);
      state->suffix = NULL;

      FT_ZERO(state);
  }


#define FTLZW_STACK_PUSH( c )                        \
  FT_BEGIN_STMNT                                     \
    if ( state->stack_top >= state->stack_size &&    \
         ft_lzwstate_stack_grow( state ) < 0   )     \
      goto Eof;                                      \
                                                     \
    state->stack[state->stack_top++] = (FT_Byte)(c); \
  FT_END_STMNT


  FT_LOCAL_DEF(FT_ULong)
      ft_lzwstate_io(FT_LzwState  state,
          FT_Byte* buffer,
          FT_ULong     out_size)
  {
      FT_ULong  result = 0;

      FT_UInt  old_char = state->old_char;
      FT_UInt  old_code = state->old_code;
      FT_UInt  in_code = state->in_code;


      if (out_size == 0)
          goto Exit;

      switch (state->phase)
      {
      case FT_LZW_PHASE_START:
      {
          FT_Byte   max_bits;
          FT_Int32  c;


          /* skip magic bytes, and read max_bits + block_flag */
          if (FT_Stream_Seek(state->source, 2) != 0 ||
              FT_Stream_TryRead(state->source, &max_bits, 1) != 1)
              goto Eof;

          state->max_bits = max_bits & LZW_BIT_MASK;
          state->block_mode = max_bits & LZW_BLOCK_MASK;
          state->max_free = (FT_UInt)((1UL << state->max_bits) - 256);

          if (state->max_bits > LZW_MAX_BITS)
              goto Eof;

          state->num_bits = LZW_INIT_BITS;
          state->free_ent = (state->block_mode ? LZW_FIRST
              : LZW_CLEAR) - 256;
          in_code = 0;

          state->free_bits = state->num_bits < state->max_bits
              ? (FT_UInt)((1UL << state->num_bits) - 256)
              : state->max_free + 1;

          c = ft_lzwstate_get_code(state);
          if (c < 0 || c > 255)
              goto Eof;

          old_code = old_char = (FT_UInt)c;

          if (buffer)
              buffer[result] = (FT_Byte)old_char;

          if (++result >= out_size)
              goto Exit;

          state->phase = FT_LZW_PHASE_CODE;
      }
      FALL_THROUGH;

      case FT_LZW_PHASE_CODE:
      {
          FT_Int32  c;
          FT_UInt   code;


      NextCode:
          c = ft_lzwstate_get_code(state);
          if (c < 0)
              goto Eof;

          code = (FT_UInt)c;

          if (code == LZW_CLEAR && state->block_mode)
          {
              /* why not LZW_FIRST-256 ? */
              state->free_ent = (LZW_FIRST - 1) - 256;
              state->buf_clear = 1;

              /* not quite right, but at least more predictable */
              old_code = 0;
              old_char = 0;

              goto NextCode;
          }

          in_code = code; /* save code for later */

          if (code >= 256U)
          {
              /* special case for KwKwKwK */
              if (code - 256U >= state->free_ent)
              {
                  /* corrupted LZW stream */
                  if (code - 256U > state->free_ent)
                      goto Eof;

                  FTLZW_STACK_PUSH(old_char);
                  code = old_code;
              }

              while (code >= 256U)
              {
                  if (!state->prefix)
                      goto Eof;

                  FTLZW_STACK_PUSH(state->suffix[code - 256]);
                  code = state->prefix[code - 256];
              }
          }

          old_char = code;
          FTLZW_STACK_PUSH(old_char);

          state->phase = FT_LZW_PHASE_STACK;
      }
      FALL_THROUGH;

      case FT_LZW_PHASE_STACK:
      {
          while (state->stack_top > 0)
          {
              state->stack_top--;

              if (buffer)
                  buffer[result] = state->stack[state->stack_top];

              if (++result == out_size)
                  goto Exit;
          }

          /* now create new entry */
          if (state->free_ent < state->max_free)
          {
              if (state->free_ent >= state->prefix_size &&
                  ft_lzwstate_prefix_grow(state) < 0)
                  goto Eof;

              FT_ASSERT(state->free_ent < state->prefix_size);

              state->prefix[state->free_ent] = (FT_UShort)old_code;
              state->suffix[state->free_ent] = (FT_Byte)old_char;

              state->free_ent += 1;
          }

          old_code = in_code;

          state->phase = FT_LZW_PHASE_CODE;
          goto NextCode;
      }

      default:  /* state == EOF */
          ;
      }

  Exit:
      state->old_code = old_code;
      state->old_char = old_char;
      state->in_code = in_code;

      return result;

  Eof:
      state->phase = FT_LZW_PHASE_EOF;
      goto Exit;
  }



#else  /* !FT_CONFIG_OPTION_USE_LZW */


  FT_EXPORT_DEF( FT_Error )
  FT_Stream_OpenLZW( FT_Stream  stream,
                     FT_Stream  source )
  {
    FT_UNUSED( stream );
    FT_UNUSED( source );

    return FT_THROW( Unimplemented_Feature );
  }


#endif /* !FT_CONFIG_OPTION_USE_LZW */


/* END */
