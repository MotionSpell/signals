#include "picture.hpp"

namespace Modules {

auto const ALIGN = 512 / 8; /*AVX-512*/

std::shared_ptr<DataPicture> DataPicture::create(OutputDefault *out, Resolution res, Resolution resInternal, PixelFormat format) {
	if (!out) return nullptr;

	auto r = out->allocData<DataPicture>(0);

	DataPicture::setup(r.get(), res, resInternal, format);

	return r;
}

void DataPicture::setup(DataPicture* r, Resolution res, Resolution resInternal, PixelFormat format) {
	// 16 bytes of padding, as required by most SIMD processing (e.g swscale)
	r->buffer->resize(PictureFormat::getSize(resInternal, format) + ALIGN + 512 / 8);

	r->format.format = format;
	r->format.res = res;
	r->setVisibleResolution(res);

	auto ptr = r->buffer->data().ptr;
	if ((uintptr_t)ptr & (ALIGN - 1))
		ptr += ALIGN - ((uintptr_t)ptr % ALIGN);

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
	default: throw std::runtime_error("Unknown pixel format for DataPicture. Please contact your vendor");
	}
}

std::shared_ptr<DataPicture> DataPicture::create(OutputDefault *out, Resolution res, PixelFormat format) {
	return create(out, res, res, format);
}
}
