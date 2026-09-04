/*----------------------------------------------
 * TJpgDec System Configurations — MiBee Cam tune
 * Grayscale output (JD_FORMAT=2) + 1/8 descaling
 * (scale=3 DC-only path) for the motion-analysis
 * grid. Original: elm-chan.org/fsw/tjpgd (ChaN,
 * BSD-style license in tjpgd.c — kept verbatim).
 *---------------------------------------------*/
#define JD_SZBUF        512
/* Specifies size of stream input buffer */

#define JD_FORMAT       2
/* Specifies output pixel format.
/  0: RGB888 (24-bit/pix)
/  1: RGB565 (16-bit/pix)
/  2: Grayscale (8-bit/pix)   <-- motion analysis
*/

#define JD_USE_SCALE    1
/* Switches output descaling feature (needed: 1/8 grid). */

#define JD_TBLCLIP      0
#define JD_FASTDECODE   1
/* 1: balanced, 3480-byte workspace (32-bit MCU) */

#if JD_FASTDECODE == 0
 #define TJPGD_WORKSPACE_SIZE 3100
#elif JD_FASTDECODE == 1
 #define TJPGD_WORKSPACE_SIZE 3480
#elif JD_FASTDECODE == 2
 #define TJPGD_WORKSPACE_SIZE (6144 + 6224)
#endif
