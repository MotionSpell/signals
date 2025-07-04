#pragma once

namespace Modules {

enum class PixelFormat : int {
	UNKNOWN = -1,
	Y8,
	I420, // YYYY YYYY YYYY YYYY UUUU UUUU VVVV VVVV
	YUV420P10LE,
	YUV422P,
	YUV422P10LE,
	YUYV422,
	NV12, // YYYY YYYY YYYY YYYY UVUV UVUV UVUV UVUV
	NV12P010LE, /*10-bit variant of NV12 with 16 bits per component (10 bits of data plus 6 LSB bits zeroed)*/
	RGB24,
	RGBA32,
	CUDA // AV_PIX_FMT_CUDA
};

}
