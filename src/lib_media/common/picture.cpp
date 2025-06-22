#include "picture.hpp"

namespace Modules {

void DataPicture::setup(DataPicture* r, Resolution res, Resolution resInternal, PixelFormat format) {
	r->format.format = format;
	r->format.res = res;
	r->setVisibleResolution(res);

	auto ptr = r->buffer->data().ptr;
	if ((uintptr_t)ptr & (PictureFormat::ALIGNMENT - 1))
		ptr += PictureFormat::ALIGNMENT - ((uintptr_t)ptr % PictureFormat::ALIGNMENT);

	switch (format) {
	case PixelFormat::Y8: {
		r->m_planes[0] = ptr;
		r->m_stride[0] = resInternal.width;
		r->m_planeCount = 1;
		break;
	}
	case PixelFormat::I420: {
		auto const numPixels = resInternal.width * resInternal.height;
		r->m_planeCount = 3;
		r->m_planes[0] = ptr;
		r->m_planes[1] = ptr + numPixels;
		r->m_planes[2] = ptr + numPixels + numPixels / 4;
		r->m_stride[0] = resInternal.width;
		r->m_stride[1] = resInternal.width / 2;
		r->m_stride[2] = resInternal.width / 2;
		break;
	}
	case PixelFormat::YUV420P10LE: {
		auto const numPlaneBytes = resInternal.width * divUp(10, 8) * resInternal.height;
		r->m_planes[0] = ptr;
		r->m_planes[1] = ptr + numPlaneBytes;
		r->m_planes[2] = ptr + numPlaneBytes + numPlaneBytes / 4;
		r->m_stride[0] = resInternal.width * divUp(10, 8);
		r->m_stride[1] = resInternal.width * divUp(10, 8) / 2;
		r->m_stride[2] = resInternal.width * divUp(10, 8) / 2;
		r->m_planeCount = 3;
		break;
	}
	case PixelFormat::YUV422P: {
		auto const numPixels = resInternal.width * resInternal.height;
		r->m_planes[0] = ptr;
		r->m_planes[1] = ptr + numPixels;
		r->m_planes[2] = ptr + numPixels + numPixels / 2;
		r->m_stride[0] = resInternal.width;
		r->m_stride[1] = resInternal.width / 2;
		r->m_stride[2] = resInternal.width / 2;
		r->m_planeCount = 3;
		break;
	}
	case PixelFormat::YUV422P10LE: {
		auto const numPixels = resInternal.width * resInternal.height;
		r->m_planes[0] = ptr;
		r->m_planes[1] = ptr + numPixels;
		r->m_planes[2] = ptr + numPixels + numPixels / 2;
		r->m_stride[0] = resInternal.width * divUp(10, 8);
		r->m_stride[1] = resInternal.width * divUp(10, 8) / 2;
		r->m_stride[2] = resInternal.width * divUp(10, 8) / 2;
		r->m_planeCount = 3;
		break;
	}
	case PixelFormat::YUYV422: {
		r->m_planes[0] = ptr;
		r->m_stride[0] = resInternal.width * 2;
		r->m_planeCount = 1;
		break;
	}
	case PixelFormat::NV12:        {
		auto const numPixels = resInternal.width * resInternal.height;
		r->m_planes[0] = ptr;
		r->m_planes[1] = ptr + numPixels;
		r->m_stride[0] = resInternal.width;
		r->m_stride[1] = resInternal.width;
		r->m_planeCount = 2;
		break;
	}
	case PixelFormat::NV12P010LE:  {
		auto const numPixels = resInternal.width * resInternal.height;
		r->m_planes[0] = ptr;
		r->m_planes[1] = ptr + numPixels * 2;
		r->m_stride[0] = resInternal.width * 2;
		r->m_stride[1] = resInternal.width * 2;
		r->m_planeCount = 2;
		break;
	}
	case PixelFormat::RGB24:       {
		r->m_planes[0] = ptr;
		r->m_stride[0] = resInternal.width * 3;
		r->m_planeCount = 1;
		break;
	}
	case PixelFormat::RGBA32:      {
		r->m_planes[0] = ptr;
		r->m_stride[0] = resInternal.width * 4;
		r->m_planeCount = 1;
		break;
	}
	case PixelFormat::CUDA:    {
		auto *ptr2 = (uintptr_t*)r->buffer->data().ptr;
		r->m_planeCount = 4;
		for (int i=0; i<r->m_planeCount; ++i) {
			r->m_planes[i] = (uint8_t*)*ptr2;
			ptr2++;
			r->m_stride[i] = (size_t)*ptr2;
			ptr2++;
		}
		break;
	}
	default: throw std::runtime_error("Unknown pixel format for DataPicture. Please contact your vendor");
	}
}
}
