#ifndef XUI_IMAGE_MEMORY_H
#define XUI_IMAGE_MEMORY_H
#include "xui.h"
#ifdef __cplusplus
extern "C" {
#endif
#define XUI_IMAGE_MEMORY_VERSION 0x00010000u
typedef struct xui_image_memory_options {
    uint32_t size, version;
    uint64_t generation;
    uint32_t format, source_width, source_height, output_width, output_height;
    uint32_t hint_width, hint_height, reserved;
} xui_image_memory_options;
typedef struct xui_image_memory_state {
    uint32_t size, version;
    uint64_t generation;
    uint32_t state, source_width, source_height, pixel_width, pixel_height, reserved;
} xui_image_memory_state;
typedef struct xui_image_memory_statistics {
    uint32_t size, version;
    uint64_t encoded_bytes, encoded_peak, encoded_limit;
} xui_image_memory_statistics;
typedef xui_status (XUI_CALL *xui_image_memory_callback)(void* context,
    const xui_image_memory_state* state, xui_string error);
/* Copies bytes before return; no path, temporary file, or borrowed caller buffer.
   Format 0=static PNG, 1=8-bit baseline/progressive grayscale/three-component JPEG.
   Generation is positive, increasing per Image, and <= INT64_MAX.
   Source <=16384/axis and 16*1024*1024 pixels; bytes <=32 MiB; decode hints 1..1024.
   Output must equal no-upscale integer-floor contain sizing for source/hints.
   Native header and codec metadata validation precede pixel allocation.
   Ready requires validated owned pixels and successful native target assignment,
   independent of child visibility. Only an unavailable target can defer readiness.
   Callback is posted/read-only: Ready(2) or initial Error(3), followed only by
   PresentationFailed(4) if a previously assigned presentation cannot recover.
   State 4 requires generation-aware host teardown after the native callback
   unwinds, not the ordinary Loading-only image-error sink. The callback slot
   remains owned after Ready until cancellation/replacement. Events are FIFO.
   Error text is borrowed for that call, UTF-8, bounded to 4096 UTF-16 units.
   Empty=0/Loading=1 are query states. Decode hints do not set layout size.
   Source aspect ratio controls contain; Image's default desired size is 192x144 DIPs.
   This version also qualifies Image's independent native axis constraints. */
XUI_API uint32_t XUI_CALL xui_image_memory_version(void) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_image_memory_set(xui_handle image,
    const xui_image_memory_options* options, const uint8_t* bytes, uint32_t length,
    xui_image_memory_callback callback, void* context) XUI_NOEXCEPT;
/* UI-thread request cancellation, valid closing/closed. Clears owned presentation
   and revokes pending callbacks before returning. A later higher-generation Set
   is allowed. Workers keep their own byte ownership until cooperative quiescence;
   cancellation never joins a codec thread or claims immediate global zero usage. */
XUI_API xui_status XUI_CALL xui_image_memory_cancel(xui_handle image) XUI_NOEXCEPT;
XUI_API xui_status XUI_CALL xui_image_memory_get_state(xui_handle image, xui_image_memory_state* state) XUI_NOEXCEPT;
/* Process-global native encoded copies, including canceled work still decoding.
   Separate from the existing bounded decoded CPU/GPU stores and managed caches. */
XUI_API xui_status XUI_CALL xui_image_memory_get_statistics(xui_image_memory_statistics* statistics) XUI_NOEXCEPT;
#ifdef __cplusplus
}
#endif
#endif
