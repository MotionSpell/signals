#include "picture.hpp"

namespace Modules {
class PictureY8 : public DataPicture {
	public:
		PictureY8(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = Y8;
		}
		PictureY8(Resolution res) : DataPicture(res, Y8) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 1;
		}
		const uint8_t* getPlane(size_t planeIdx) const override {
			return m_planes[planeIdx];
		}
		uint8_t* getPlane(size_t planeIdx) override {
			return m_planes[planeIdx];
		}
		size_t getPitch(size_t planeIdx) const override {
			return m_pitch[planeIdx];
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			resize(internalFormat.getSize());
			m_planes[0] = data();
			m_pitch[0] = res.width;
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}

	private:
		size_t m_pitch[1];
		uint8_t* m_planes[1];
};

class PictureYUV420P10LE : public DataPicture {
	public:
		PictureYUV420P10LE(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = YUV420P10LE;
		}
		PictureYUV420P10LE(Resolution res) : DataPicture(res, YUV420P10LE) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 3;
		}
		const uint8_t* getPlane(size_t planeIdx) const override {
			return m_planes[planeIdx];
		}
		uint8_t* getPlane(size_t planeIdx) override {
			return m_planes[planeIdx];
		}
		size_t getPitch(size_t planeIdx) const override {
			return m_pitch[planeIdx];
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			resize(internalFormat.getSize());
			auto const numPlaneBytes = res.width * divUp(10, 8) * res.height;
			m_planes[0] = data();
			m_planes[1] = data() + numPlaneBytes;
			m_planes[2] = data() + numPlaneBytes + numPlaneBytes / 4;
			m_pitch[0] = res.width * divUp(10, 8);
			m_pitch[1] = res.width * divUp(10, 8) / 2;
			m_pitch[2] = res.width * divUp(10, 8) / 2;
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}

	private:
		size_t m_pitch[3];
		uint8_t* m_planes[3];
};

class PictureYUV422P : public DataPicture {
	public:
		PictureYUV422P(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = YUV422P;
		}
		PictureYUV422P(Resolution res) : DataPicture(res, YUV422P) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 3;
		}
		const uint8_t* getPlane(size_t planeIdx) const override {
			return m_planes[planeIdx];
		}
		uint8_t* getPlane(size_t planeIdx) override {
			return m_planes[planeIdx];
		}
		size_t getPitch(size_t planeIdx) const override {
			return m_pitch[planeIdx];
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			resize(internalFormat.getSize());
			auto const numPixels = res.width * res.height;
			m_planes[0] = data();
			m_planes[1] = data() + numPixels;
			m_planes[2] = data() + numPixels + numPixels / 2;
			m_pitch[0] = res.width;
			m_pitch[1] = res.width / 2;
			m_pitch[2] = res.width / 2;
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}

	private:
		size_t m_pitch[3];
		uint8_t* m_planes[3];
};

class PictureYUV422P10LE : public DataPicture {
	public:
		PictureYUV422P10LE(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = YUV422P10LE;
		}
		PictureYUV422P10LE(Resolution res) : DataPicture(res, YUV422P10LE) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 3;
		}
		const uint8_t* getPlane(size_t planeIdx) const override {
			return m_planes[planeIdx];
		}
		uint8_t* getPlane(size_t planeIdx) override {
			return m_planes[planeIdx];
		}
		size_t getPitch(size_t planeIdx) const override {
			return m_pitch[planeIdx];
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			resize(internalFormat.getSize());
			auto const numPixels = res.width * res.height;
			m_planes[0] = data();
			m_planes[1] = data() + numPixels;
			m_planes[2] = data() + numPixels + numPixels / 2;
			m_pitch[0] = res.width * divUp(10, 8);
			m_pitch[1] = res.width * divUp(10, 8) / 2;
			m_pitch[2] = res.width * divUp(10, 8) / 2;
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}

	private:
		size_t m_pitch[3];
		uint8_t* m_planes[3];
};

class PictureYUYV422 : public DataPicture {
	public:
		PictureYUYV422(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = YUYV422;
		}
		PictureYUYV422(Resolution res) : DataPicture(res, YUYV422) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 1;
		}
		const uint8_t* getPlane(size_t /*planeIdx*/) const override {
			return data();
		}
		uint8_t* getPlane(size_t /*planeIdx*/) override {
			return data();
		}
		size_t getPitch(size_t /*planeIdx*/) const override {
			return internalFormat.res.width * 2;
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			resize(internalFormat.getSize());
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}
};

class PictureNV12 : public DataPicture {
	public:
		PictureNV12(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = NV12;
		}
		PictureNV12(Resolution res) : DataPicture(res, NV12) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 2;
		}
		const uint8_t* getPlane(size_t planeIdx) const override {
			return m_planes[planeIdx];
		}
		uint8_t* getPlane(size_t planeIdx) override {
			return m_planes[planeIdx];
		}
		size_t getPitch(size_t planeIdx) const override {
			return m_pitch[planeIdx];
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			resize(internalFormat.getSize());
			auto const numPixels = res.width * res.height;
			m_planes[0] = data();
			m_planes[1] = data() + numPixels;
			m_pitch[0] = m_pitch[1] = res.width;
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}

	private:
		size_t m_pitch[2];
		uint8_t* m_planes[2];
};

class PictureRGB24 : public DataPicture {
	public:
		PictureRGB24(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = RGB24;
		}
		PictureRGB24(Resolution res) : DataPicture(res, RGB24) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 1;
		}
		const uint8_t* getPlane(size_t /*planeIdx*/) const override {
			return data();
		}
		uint8_t* getPlane(size_t /*planeIdx*/) override {
			return data();
		}
		size_t getPitch(size_t /*planeIdx*/) const override {
			return internalFormat.res.width * 3;
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			// 16 bytes of padding, as required by most SIMD processing (e.g swscale)
			resize(internalFormat.getSize() + 16);
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}
};

class PictureRGBA32 : public DataPicture {
	public:
		PictureRGBA32(size_t /*unused*/) : DataPicture(0) {
			internalFormat.format = format.format = RGBA32;
		}
		PictureRGBA32(Resolution res) : DataPicture(res, RGBA32) {
			setInternalResolution(res);
			setVisibleResolution(res);
		}
		size_t getNumPlanes() const override {
			return 1;
		}
		const uint8_t* getPlane(size_t /*planeIdx*/) const override {
			return data();
		}
		uint8_t* getPlane(size_t /*planeIdx*/) override {
			return data();
		}
		size_t getPitch(size_t /*planeIdx*/) const override {
			return internalFormat.res.width * 4;
		}
		void setInternalResolution(Resolution res) override {
			internalFormat.res = res;
			resize(internalFormat.getSize());
		}
		void setVisibleResolution(Resolution res) override {
			format.res = res;
		}
};

std::shared_ptr<DataPicture> DataPicture::create(OutputPicture *out, Resolution res, Resolution resInternal, PixelFormat format) {
	if (!out) return nullptr;
	std::shared_ptr<DataPicture> r;
	auto const size = PictureFormat::getSize(resInternal, format);
	switch (format) {
	case Y8:          r = out->getBuffer<PictureY8>(size);          break;
	case YUV420P:     r = out->getBuffer<PictureYUV420P>(size);     break;
	case YUV420P10LE: r = out->getBuffer<PictureYUV420P10LE>(size); break;
	case YUV422P:     r = out->getBuffer<PictureYUV422P>(size);     break;
	case YUV422P10LE: r = out->getBuffer<PictureYUV422P10LE>(size); break;
	case YUYV422:     r = out->getBuffer<PictureYUYV422>(size);     break;
	case NV12:        r = out->getBuffer<PictureNV12   >(size);     break;
	case RGB24:       r = out->getBuffer<PictureRGB24  >(size);     break;
	case RGBA32:      r = out->getBuffer<PictureRGBA32 >(size);     break;
	default: throw std::runtime_error("Unknown pixel format for DataPicture. Please contact your vendor");
	}
	r->setInternalResolution(resInternal);
	r->setVisibleResolution(res);
	return r;
}

std::shared_ptr<DataPicture> DataPicture::create(OutputPicture *out, Resolution res, PixelFormat format) {
	return create(out, res, res, format);
}
}
