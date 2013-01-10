#ifndef COMMON_COLOR_HPP_
#define COMMON_COLOR_HPP_
//----------------------------------------------------------------------------//
#include <Danvil/Color.h>
#include <Eigen/Dense>
#include <Slimage/Slimage.hpp>
//----------------------------------------------------------------------------//
namespace common {
//----------------------------------------------------------------------------//

/** Computes a color to express a similarity value between 0 and 1 */
inline Eigen::Vector3f SimilarityColor(float x)
{
	static auto cm = Danvil::ContinuousIntervalColorMapping<float, float>::Factor_Black_Blue_Red_Yellow_White();
	cm.setRange(0.0f, 1.0f);
	Danvil::Colorf color = cm(x);
	return {color.r,color.g,color.b};
}

inline Eigen::Vector3f IntensityColor(float x)
{
	static auto cm = Danvil::ContinuousIntervalColorMapping<float, float>::Factor_Black_Blue_Red_Yellow_White();
	cm.setRange(0.0f, 1.0f);
	Danvil::Colorf color = cm(x);
	return {color.r,color.g,color.b};
}

template<typename T>
inline Eigen::Vector3f CountColor(T num, T min, T max)
{
	return IntensityColor(
		(num < min)
			? 0.0f
			: static_cast<float>(num - min)/static_cast<float>(max-min)
	);
}

inline slimage::Pixel3ub ColorToPixel(const Eigen::Vector3f& color) {
	return slimage::Pixel3ub{{
		static_cast<unsigned char>(255.f*std::min(1.0f, std::max(0.0f, color[0]))),
		static_cast<unsigned char>(255.f*std::min(1.0f, std::max(0.0f, color[1]))),
		static_cast<unsigned char>(255.f*std::min(1.0f, std::max(0.0f, color[2])))
	}};
}

inline slimage::Image3ub ColorizeDepth(const slimage::Image1ui16& img16, uint16_t min, uint16_t max) {
	slimage::Image3ub img(img16.width(), img16.height());
	const int n = img16.size();
	for(int i=0; i<n; i++) {
		img[i] = ColorToPixel(CountColor((uint16_t)img16[i], min, max));
	}
	return img;
}

//----------------------------------------------------------------------------//
}
//----------------------------------------------------------------------------//
#endif
