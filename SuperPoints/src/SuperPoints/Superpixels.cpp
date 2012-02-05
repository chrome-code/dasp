/*
 * Superpixel.cpp
 *
 *  Created on: Jan 26, 2012
 *      Author: david
 */

#include "Superpixels.hpp"
#include "Mipmaps.hpp"
#include <Danvil/Tools/MoreMath.h>
#include <Danvil/Tools/FunctionCache.h>
//#include <Danvil/Images/ImageOps.h>
#include <boost/random.hpp>

namespace dasp {

void Cluster::UpdateCenter(const ImagePoints& points)
{
	if(!is_valid()) {
		return;
	}
	float old_scala = center.scala;
	center = Point::Zero();
	unsigned int n_valid = 0;
	for(unsigned int i : pixel_ids) {
		const Point& p = points[i];
		if(p.valid()) {
			center += p;
			n_valid ++;
		}
		else {
			center.color += p.color;
			center.pos += p.pos;
		}
	}
	center.color /= float(pixel_ids.size());
	center.pos /= float(pixel_ids.size());
	if(n_valid > 0) {
		center.depth /= float(n_valid);
//		center.world *= 1.0f / float(n_valid);
		center.normal.normalize(); // FIXME normalization may fail!
	}
	else {
		center.depth = 0.0f;
//		center.world = Eigen::Vector3f::Zero();
		center.normal = Eigen::Vector3f::Zero();
	}
//	// get scala of center
//	center.scala = points(center.spatial_x(), center.spatial_y()).scala;
	center.scala = old_scala;
}

ParametersExt ComputeParameters(const Parameters& opt, unsigned int width, unsigned int height)
{
	ParametersExt opt_ext(opt);
	opt_ext.width = width;
	opt_ext.height = height;
	float d = std::sqrt(float(opt_ext.width*opt_ext.height) / float(opt_ext.cluster_count));
	opt_ext.cluster_nx = (unsigned int)std::ceil(float(opt_ext.width) / d);
	opt_ext.cluster_ny = (unsigned int)std::ceil(float(opt_ext.height) / d);
	opt_ext.cluster_dx = (unsigned int)std::floor(float(opt_ext.width) / float(opt_ext.cluster_nx));
	opt_ext.cluster_dy = (unsigned int)std::floor(float(opt_ext.height) / float(opt_ext.cluster_ny));
	opt_ext.cluster_count = opt_ext.cluster_nx * opt_ext.cluster_ny;
	opt_ext.radius = std::sqrt(float(opt_ext.cluster_dx*opt_ext.cluster_dx + opt_ext.cluster_dy*opt_ext.cluster_dy));
	opt_ext.spatial_normalizer = 1.0f / opt_ext.radius;
	opt_ext.weight_spatial_final = opt.weight_spatial * opt_ext.spatial_normalizer;
	return opt_ext;
}

ImagePoints CreatePoints(
		const slimage::Image3ub& image,
		const slimage::Image1ui16& depth,
//		const slimage::Image3fPtr& pos,
		const slimage::Image3f& normals,
		const ParametersExt& opt)
{
	unsigned int width = image.width();
	unsigned int height = image.height();
	assert(width == depth.width() && height == depth.height());
//	assert(width == pos->width() && height == pos->height());
	if(!normals.isNull()) {
		assert(width == normals.width() && height == normals.height());
	}

	ImagePoints points(width, height);

	const unsigned char* p_col = image.begin();
	const uint16_t* p_depth = depth.begin();
//	const float* p_points = pos->begin();
	const float pixel_size_factor = opt.computePixelSizeFactor();
	const float* p_normals = normals.isNull() ? 0 : normals.begin();
	for(unsigned int y=0; y<height; y++) {
		for(unsigned int x=0; x<width; x++, p_col+=3, p_depth++) {
			Point& p = points(x, y);
//			p.valid_ = true;//(*p_depth > 0);
			p.color[0] = float(p_col[0]) / 255.0f;
			p.color[1] = float(p_col[1]) / 255.0f;
			p.color[2] = float(p_col[2]) / 255.0f;
			uint16_t d = *p_depth;
			p.depth = float(d) * 0.001f;
			p.scala = (d > 0) ? (pixel_size_factor / p.depth) : 0;
//			p.world[0] = p.depth*(float(x) - float(width/2))*scl;
//			p.world[1] = p.depth*(float(y) - float(height/2))*scl;
//			p.world[2] = p.depth;
//			if(p_normals != 0) {
//				p.world[0] = p_points[0];
//				p.world[1] = p_points[1];
//				p.world[2] = p_points[2];
//				p_points += 3;
//			}
			if(p_normals != 0) {
				p.normal[0] = p_normals[0];
				p.normal[1] = p_normals[1];
				p.normal[2] = p_normals[2];
				p_normals += 3;
			}
			else {
				p.normal[0] = 0;
				p.normal[1] = 0;
				p.normal[2] = -1.0f;
			}
		}
	}

	return points;
}

std::vector<Cluster> ComputeSuperpixels(const ImagePoints& points, const slimage::Image1f& edges, const ParametersExt& opt)
{
	std::vector<Seed> seeds = FindSeeds(points, opt);
	if(!edges.isNull()) {
		ImproveSeeds(seeds, points, edges, opt);
	}
	return ComputeSuperpixels(points, seeds, opt);
}

std::vector<Cluster> ComputeSuperpixels(const ImagePoints& points, const std::vector<Seed>& seeds, const ParametersExt& opt)
{
	std::vector<Cluster> clusters = CreateClusters(seeds, points, opt);
	for(unsigned int i=0; i<opt.iterations; i++) {
		MoveClusters(clusters, points, opt);
	}
	return clusters;
}

std::vector<int> ComputePixelLabels(const std::vector<Cluster>& clusters, const ImagePoints& points)
{
	std::vector<int> labels(points.size(), -1);
	for(unsigned int j=0; j<clusters.size(); j++) {
		for(unsigned int i : clusters[j].pixel_ids) {
			labels[i] = int(j);
		}
	}
	return labels;
}

std::vector<Seed> FindSeedsGrid(const ImagePoints& points, const ParametersExt& opt)
{
	const unsigned int Dx = opt.cluster_dx;
	const unsigned int Dy = opt.cluster_dy;
	const unsigned int Hx = Dx/2;
	const unsigned int Hy = Dy/2;
	const float S = float(std::max(Dx, Dy));

	// space seeds evently
	std::vector<Seed> seeds;
	seeds.reserve(opt.cluster_count);
	for(unsigned int iy=0; iy<opt.cluster_ny; iy++) {
		unsigned int y = Hy + Dy * iy;
		for(unsigned int ix=0; ix<opt.cluster_nx; ix++) {
			unsigned int x = Hx + Dx * ix;
			Seed p;
			p.x = x;
			p.y = y;
			p.scala = S;
			seeds.push_back(p);
		}
	}

	return seeds;
}

std::vector<Seed> FindSeedsDepthRandom(const ImagePoints& points, const ParametersExt& opt)
{
	assert(false && "FindSeedsRandom: Not implemented!");
//	static boost::mt19937 rng;
//	constexpr float cCameraFocal = 25.0f;
//	// for each pixel compute number of expected clusters
//	std::vector<float> cdf(points.size());
//	for(unsigned int i=0; i<points.size(); i++) {
//		uint16_t zi = *(depth->begin() + i);
//		float z = 0.001f * float(zi);
//		float v = z * z;
//		cdf[i] = (i == 0) ? v : (v + cdf[i-1]);
//	}
//	float sum = cdf[cdf.size() - 1];
//	boost::uniform_real<float> rnd(0.0f, sum);
//	boost::variate_generator<boost::mt19937&, boost::uniform_real<float> > die(rng, rnd);
//	// randomly pick clusters based on probability
//	std::vector<Seed> seeds;
//	seeds.reserve(opt.cluster_count);
//	while(seeds.size() < opt.cluster_count) {
//		Seed s;
//		float rnd = die();
//		auto it = std::lower_bound(cdf.begin(), cdf.end(), rnd);
//		unsigned int index = it - cdf.begin() - 1;
//		uint16_t zi = *(depth->begin() + index);
//		if(zi == 0) {
//			continue;
//		}
//		float z = 0.001f * float(zi);
//		s.x = index % opt.width;
//		s.y = index / opt.width;
//		s.radius = cCameraFocal / z;
//		seeds.push_back(s);
//	}
//	return seeds;
}

void FindSeedsBlueGrid_WalkMipmaps(
		const ImagePoints& points,
		std::vector<Seed>& seeds,
		const std::vector<slimage::Image1f>& mipmaps,
		int level, unsigned int x, unsigned int y)
{
	static boost::mt19937 rng;
	static boost::uniform_real<float> rnd(0.0f, 1.0f);
	static boost::variate_generator<boost::mt19937&, boost::uniform_real<float> > die(rng, rnd);

	const slimage::Image1f& mm = mipmaps[level];

	float v = mm(x, y);

	if(v > 1.0f && level > 1) { // do not access mipmap 0!
		// go down
		FindSeedsBlueGrid_WalkMipmaps(points, seeds, mipmaps, level - 1, 2*x,     2*y    );
		FindSeedsBlueGrid_WalkMipmaps(points, seeds, mipmaps, level - 1, 2*x,     2*y + 1);
		FindSeedsBlueGrid_WalkMipmaps(points, seeds, mipmaps, level - 1, 2*x + 1, 2*y    );
		FindSeedsBlueGrid_WalkMipmaps(points, seeds, mipmaps, level - 1, 2*x + 1, 2*y + 1);
	}
	else {
		if(die() < v)
		{
			// create seed in the middle
			Seed s;
			unsigned int half = (1 << (level - 1));
			s.x = (x << level) + half;
			s.y = (y << level) + half;
			if(s.x < int(points.width()) && s.y < int(points.height())) {
				s.scala = points(s.x, s.y).scala;
//				std::cout << s.x << " " << s.y << " " << s.radius << " " << points(s.x, s.y).scala << " " << points(s.x, s.y).depth << std::endl;
				if(s.scala > 2.0f) {
					seeds.push_back(s);
				}
			}
		}
	}
}

std::vector<Seed> FindSeedsDepthMipmap(const ImagePoints& points, const ParametersExt& opt)
{
	// compute estimated number of seeds per pixel
	slimage::Image1f num(points.width(), points.height());
	for(unsigned int i=0; i<points.size(); i++) {
		num[i] = points[i].estimatedCount();
	}
	// compute mipmaps
	std::vector<slimage::Image1f> mipmaps = Mipmaps::ComputeMipmaps(num, 0);
	// now create pixel seeds
	std::vector<Seed> seeds;
	FindSeedsBlueGrid_WalkMipmaps(points, seeds, mipmaps, mipmaps.size() - 1, 0, 0);
	return seeds;
}

namespace BlueNoise
{
	constexpr unsigned int D = 2;

	struct Point {
		float x, y;
		float weight;
		float scale;
	};

	inline
	float KernelFunctorImpl(float d) {
		constexpr float A = 0.39894228f;
		return A * std::exp(-0.5f*d*d);
	}

	inline
	float KernelFunctor(float d) {
		static Danvil::FunctionCache<float,1> cache(0.0f, 5.0f, &KernelFunctorImpl);
		return cache(std::abs(d));
	}

	inline
	float KernelFunctorSquareImpl(float d) {
		constexpr float A = 0.39894228f;
		return A * std::exp(-0.5f*d);
	}

	inline
	float KernelFunctorSquare(float d) {
		static Danvil::FunctionCache<float,1> cache(0.0f, 20.0f, &KernelFunctorSquareImpl);
		return cache(std::abs(d));
	}

	inline
	float ZeroBorderAccess(const slimage::Image1f& density, int x, int y) {
		if(0 <= x && x < int(density.width()) && 0 <= y && y < int(density.height())) {
			return density(x,y);
		}
		else {
			return 0.0f;
		}
	}

	inline
	float KernelScaleFunction(float roh, float weight) {
		return std::pow(roh / weight, -1.0f / float(D));
	}

	inline
	float ScalePowerD(float s) {
		//return std::pow(s, -float(D));
		return 1.0f / (s*s);
	}

	inline
	float EnergyApproximation(const std::vector<Point>& pnts, float x, float y) {
		float sum = 0.0f;
		for(const Point& p : pnts) {
			float dx = p.x - x;
			float dy = p.y - y;
			float ka = ScalePowerD(p.scale);
//			float k_arg = std::sqrt(dx*dx + dy*dy) / p.scale;
//			float k_val = KernelFunctor(k_arg);
			float k_arg_square = (dx*dx + dy*dy) / (p.scale*p.scale);
			float k_val = KernelFunctorSquare(k_arg_square);
			sum += ka * k_val;
		}
		return sum;
	}

	inline
	float Energy(const std::vector<Point>& pnts, const slimage::Image1f& density) {
		float error = 0.0f;
		for(unsigned int y=0; y<density.height(); y++) {
			for(unsigned int x=0; x<density.width(); x++) {
				float px = float(x);
				float py = float(y);
				float a = EnergyApproximation(pnts, px, py);
				float roh = density(x, y);
				error += std::abs(a - roh);
			}
		}
		return error;
	}

	void EnergyDerivative(const std::vector<Point>& pnts, const slimage::Image1f& density, unsigned int i, float& result_dE_x, float& result_dE_y) {
		float dE_x = 0.0f;
		float dE_y = 0.0f;
		float px = pnts[i].x;
		float py = pnts[i].y;
		float ps = pnts[i].scale;
//		float ps_scl = 1.0f / ps;
		float ps_scl = 1.0f / (ps * ps);
		for(unsigned int y=0; y<density.height(); y++) {
			for(unsigned int x=0; x<density.width(); x++) {
				float ux = float(x);
				float uy = float(y);
				float dx = ux - px;
				float dy = uy - py;
//				float k_arg = std::sqrt(dx*dx + dy*dy) * ps_scl;
//				float k_val = KernelFunctor(k_arg);
				float k_arg_square = (dx*dx + dy*dy) * ps_scl;
				float k_val = KernelFunctorSquare(k_arg_square);
				float apx = EnergyApproximation(pnts, ux, uy);
				float roh = density(x, y);
				if(apx < roh) {
					k_val = -k_val;
				}
				dE_x += k_val * dx;
				dE_y += k_val * dy;
			}
		}
		float A = 1.0f / std::pow(ps, float(D + 1));
		result_dE_x = A * dE_x;
		result_dE_y = A * dE_y;
	}

	std::vector<Point> PlacePoints(const slimage::Image1f& density, unsigned int p) {
		// access original index in a random order
		std::vector<unsigned int> indices(density.size());
		for(unsigned int i=0; i<indices.size(); i++) {
			indices[i] = i;
		}
		std::random_shuffle(indices.begin(), indices.end());
		// compute points
		std::vector<Point> pnts;
		pnts.reserve(indices.size());
		// compute current error in density
		float error_current = Energy(pnts, density);
		std::cout << "INITIAL ERROR: " << error_current << std::endl;
		// try add kernel points
		for(unsigned int i : indices) {
			float roh = density[i];
			if(roh == 0) {
//				std::cout << i << " roh is 0!" << std::endl;
				continue;
			}
			Point u;
			int q = roh < 1 ? 0 : std::ceil(std::log2(roh) / float(D));
			u.x = float(i % density.width());
			u.y = float(i / density.width());
			u.weight = float(1 << (D*q));
			u.scale = KernelScaleFunction(roh, u.weight);
			// try to add
			pnts.push_back(u);
			// check if the points reduced the energy
			float error_new = Energy(pnts, density);
			if(error_new > error_current) {
				// reject
				pnts.pop_back();
				std::cout << u.x << " " << u.y << " " << u.weight << " " << error_new << " REJECTED" << std::endl;
			}
			else {
				error_current = error_new;
				std::cout << u.x << " " << u.y << " " << u.weight << " " << error_new << std::endl;
			}
		}
		return pnts;
	}

	void Refine(std::vector<Point>& points, const slimage::Image1f& density) {
		static boost::mt19937 rng;
		static boost::normal_distribution<float> rnd(0.0f, 1.0f);
		static boost::variate_generator<boost::mt19937&, boost::normal_distribution<float> > die(rng, rnd);
		constexpr float dt = 1.0f;
		constexpr float T = 0.5f;
		for(unsigned int i=0; i<points.size(); i++) {
			Point& p = points[i];
			float c0 = dt * p.scale;
			float cA = c0 / 2.0f;
			float cB = std::sqrt(T * c0);
			float dx, dy;
			EnergyDerivative(points, density, i, dx, dy);
			p.x = p.x - cA * dx + cB * die();
			p.y = p.y - cA * dy + cB * die();
		}
	}

	std::vector<Point> Split(const std::vector<Point>& points, const slimage::Image1f& density, bool& result_added) {
		std::vector<Point> pnts_new;
		result_added = false;
		for(Point u : points) {
			if(u.weight > 1.0f) {
				result_added = true;
				u.x *= 2.0f;
				u.y *= 2.0f;
				u.weight /= float(1 << D);
				constexpr float A = 0.70710678f;
				constexpr float Delta[4][2] = {
						{-A, -A}, {+A, -A}, {-A, +A}, {+A, +A}
				};
				for(unsigned int i=0; i<4; i++) {
					Point ui = u;
					ui.x += u.scale * Delta[i][0];
					ui.y += u.scale * Delta[i][1];
					float roh = ZeroBorderAccess(density, int(ui.x), int(ui.y));
					if(roh > 0) {
						ui.scale = KernelScaleFunction(roh, ui.weight);
						pnts_new.push_back(ui);
					}
				}
			}
			else {
				u.x *= 2.0f;
				u.y *= 2.0f;
				u.weight = 1.0f;
				float roh = ZeroBorderAccess(density, int(u.x), int(u.y));
				if(roh > 0) {
					u.scale = KernelScaleFunction(roh, u.weight);
					pnts_new.push_back(u);
				}
			}
		}
		return pnts_new;
	}

	std::vector<Point> Compute(const slimage::Image1f& density) {
		// compute mipmaps
		std::vector<slimage::Image1f> mipmaps = Mipmaps::ComputeMipmaps(density, 4);
		int p = int(mipmaps.size()) - 1;
		std::vector<Point> pnts;
		for(int i=p; i>=0; i--) {
			std::cout << "Blue noise step " << i << "... " << std::flush;
			bool need_refinement;
			if(i == p) {
				// place initial points
				pnts = PlacePoints(mipmaps[i], i);
				need_refinement = true;
			}
			else {
				// split points
				pnts = Split(pnts, mipmaps[i], need_refinement);
			}
			// refine points for new density map
			if(need_refinement) {
				Refine(pnts, mipmaps[i]);
			}
			std::cout << pnts.size() << " points." << std::endl;
		}
		return pnts;
	}

}

std::vector<Seed> FindSeedsDepthBlue(const ImagePoints& points, const ParametersExt& opt)
{
	// compute estimated number of seeds per pixel
	slimage::Image1f num(points.width(), points.height());
	for(unsigned int i=0; i<points.size(); i++) {
		num[i] = points[i].estimatedCount();
	}
	// compute blue noise points
	std::vector<BlueNoise::Point> pnts = BlueNoise::Compute(num);
	// convert to seeds
	std::vector<Seed> seeds;
	seeds.reserve(pnts.size());
	for(unsigned int i=0; i<pnts.size(); i++) {
		Seed s;
		s.x = std::round(pnts[i].x);
		s.y = std::round(pnts[i].y);
		if(0 <= s.x && s.x < points.width() && 0 <= s.y && s.y < points.height()) {
			s.scala = points(s.x, s.y).scala;
			seeds.push_back(s);
		}
	}
	return seeds;
}

std::vector<Seed> FindSeeds(const ImagePoints& points, const ParametersExt& opt)
{
	switch(opt.seed_mode) {
	case SeedModes::EquiDistant:
		return FindSeedsGrid(points, opt);
	case SeedModes::DepthDependentShooting:
		return FindSeedsDepthRandom(points, opt);
	case SeedModes::DepthDependentMipmap:
		return FindSeedsDepthMipmap(points, opt);
	case SeedModes::BlueNoise:
		return FindSeedsDepthBlue(points, opt);
	default:
		assert(false && "FindSeeds: Unkown mode!");
	};
}

std::vector<Cluster> CreateClusters(const std::vector<Seed>& seeds, const ImagePoints& points, const ParametersExt& opt)
{
	// create clusters
	std::vector<Cluster> clusters;
	clusters.reserve(opt.cluster_count);
	for(const Seed& p : seeds) {
		Cluster c;
//		c.center.valid_ = true;
		c.center.pos[0] = float(p.x);
		c.center.pos[1] = float(p.y);
		c.center.scala = p.scala;
		// assign points
		int R = c.center.radius() / 2;
		assert(R >= 0 && "CreateClusters: Invalid radius!");
		c.pixel_ids.reserve((2*R + 1)*(2*R + 1));
		unsigned int xmin = std::max<int>(p.x - R, 0);
		unsigned int xmax = std::min<int>(p.x + R, int(opt.width) - 1);
		unsigned int ymin = std::max<int>(p.y - R, 0);
		unsigned int ymax = std::min<int>(p.y + R, int(opt.height) - 1);
		for(unsigned int yi=ymin; yi<=ymax; yi++) {
			for(unsigned int xi=xmin; xi<=xmax; xi++) {
				unsigned int index = points.index(xi, yi);
				c.pixel_ids.push_back(index);
			}
		}
		// update center
		c.UpdateCenter(points);
		if(c.is_valid()) {
			clusters.push_back(c);
		}
	}
	return clusters;
}

void ComputeEdges(const ImagePoints& points, slimage::Image1f& edges, const ParametersExt& opt, slimage::ThreadingOptions threadopt)
{
	const unsigned int width = points.width();
	const unsigned int height = points.height();

	// compute edges strength
	edges.resize(width, height);
	float* p_edge_begin = edges.begin();
	slimage::ParallelProcess(edges, [p_edge_begin,width,height,&points,&opt](float* p_edge) {
		int i = p_edge - p_edge_begin;
		int x = i % width;
		int y = i / width;
		float v = 0.0f;
		if(x-1 < 0 || int(width) <= x+1 || y-1 < 0 || int(height) <= y+1) {
			v = 1e9; // dont want to be here
		}
		else {
			const Point& px1 = points(x-1, y);
			const Point& px2 = points(x+1, y);
			const Point& py1 = points(x, y-1);
			const Point& py2 = points(x, y+1);
//			if(!px1.valid || !px2.valid || !py1.valid || !py2.valid) {
//				v = 1e9; // dont want to be here
//			}
//			else {
				float dx = Distance(px1, px2, opt);
				float dy = Distance(py1, py2, opt);
				v = dx + dy;
//			}
		}
		*p_edge = v;
	}, threadopt);
}

void ImproveSeeds(std::vector<Seed>& seeds, const ImagePoints& points, const slimage::Image1f& edges, const ParametersExt& opt)
{
	const unsigned int width = points.width();
	const unsigned int height = points.height();

	const int dx8[8] = {-1, -1,  0,  1, 1, 1, 0, -1};
	const int dy8[8] = { 0, -1, -1, -1, 0, 1, 1,  1};

	const float* p_edges = edges.begin();

	for(Seed& seed : seeds) {
		int sx = seed.x;
		int sy = seed.y;
		int bestid = sy*width + sx;
		for(unsigned int i=0; i<8; i++) {
			int nx = sx + dx8[i];
			int ny = sy + dy8[i];
			if(nx >= 0 && nx < int(width) && ny >= 0 && ny < int(height)) {
				int nind = ny*width + nx;
				if(p_edges[nind] < p_edges[bestid]) {
					bestid = nind;
				}
			}
		}
		seed.x = bestid % width;
		seed.y = bestid / width;
	}
}

void MoveClusters(std::vector<Cluster>& clusters, const ImagePoints& points, const ParametersExt& opt)
{
	std::vector<float> v_dist(points.size(), 1e9);
	std::vector<int> v_label(points.size(), -1);
	// for each cluster check possible points
	for(unsigned int j=0; j<clusters.size(); j++) {
		const Cluster& c = clusters[j];
		int cx = c.center.spatial_x();
		int cy = c.center.spatial_y();
		int R = int(c.center.scala * opt.coverage);
		unsigned int xmin = std::max(0, cx - R);
		unsigned int xmax = std::min(int(points.width()), cx + R);
		unsigned int ymin = std::max(0, cy - R);
		unsigned int ymax = std::min(int(points.height()), cy + R);
		for(unsigned int y=ymin; y<ymax; y++) {
			for(unsigned int x=xmin; x<xmax; x++) {
				unsigned int i = points.index(x,y);
				const Point& p = points[i];
				if(!p.valid()) {
					// omit invalid points
					continue;
				}
				float dist = Distance(p, c, opt);
				if(dist < v_dist[i]) {
					v_dist[i] = dist;
					v_label[i] = j;
				}
			}
		}
	}
	// delete clusters assignments
	for(Cluster& c : clusters) {
		c.pixel_ids.clear();
	}
	// assign points to clusters
	for(unsigned int i=0; i<points.size(); i++) {
		int label = v_label[i];
		if(label >= 0) {
			clusters[label].pixel_ids.push_back(i);
		}
	}
	// update cluster centers and remove invalid clusters
	std::vector<Cluster> clusters_valid;
	clusters_valid.reserve(clusters.size());
	for(unsigned int i=0; i<clusters.size(); i++) {
		Cluster c = clusters[i];
		c.UpdateCenter(points);
		if(c.is_valid()) {
			clusters_valid.push_back(c);
		}
	}
	clusters = clusters_valid;
}

void PlotCluster(const Cluster& cluster, const ImagePoints& points, const slimage::Image3ub& img)
{
	assert(cluster.is_valid());
	// plot all pixels belonging to the cluster in the color of the cluster center
	unsigned char c_col_r = 255.0f * cluster.center.color[0];
	unsigned char c_col_g = 255.0f * cluster.center.color[1];
	unsigned char c_col_b = 255.0f * cluster.center.color[2];
	for(unsigned int i : cluster.pixel_ids) {
		const Point& p = points[i];
		unsigned char* col = img.pointer(p.spatial_x(), p.spatial_y());
//			cols[k % cols.size()].writeRgb(col);
		col[0] = c_col_r;
		col[1] = c_col_g;
		col[2] = c_col_b;
	}
	// plot the cluster center (using some kind of inverse color)
	int cx = cluster.center.spatial_x();
	int cy = cluster.center.spatial_y();
	if(0 <= cx && cx < int(points.width()) && 0 <= cy && cy < int(points.height())) {
		unsigned char* col = img.pointer(cx, cy);
		col[0] = 255 - c_col_r;
		col[1] = 255 - c_col_g;
		col[2] = 255 - c_col_b;
	}

}

void PlotCluster(const std::vector<Cluster>& clusters, const ImagePoints& points, const slimage::Image3ub& img)
{
//	std::vector<Danvil::ColorUB> cols = {
//			Danvil::Color::Red, Danvil::Color::Green, Danvil::Color::Blue, Danvil::Color::Yellow, Danvil::Color::Cyan, Danvil::Color::Magenta, Danvil::Color::Black
//	};
	img.fill(0);
	for(const Cluster& x : clusters) {
		PlotCluster(x, points, img);
	}
}

void PlotEdges(const std::vector<int>& labels, const slimage::Image3ub& img, unsigned int edge_w, unsigned char edge_r, unsigned char edge_g, unsigned char edge_b)
{
	const int dx8[8] = {-1, -1,  0,  1, 1, 1, 0, -1};
	const int dy8[8] = { 0, -1, -1, -1, 0, 1, 1,  1};

	unsigned int width = img.width();
	unsigned int height = img.height();

	std::vector<bool> istaken(width*height, false);

	unsigned int i = 0;
	for(unsigned int y=0; y<height; y++) {
		for(unsigned int x=0; x<width; x++, i++) {
			unsigned int np = 0;
			for(unsigned int k=0; k<8; k++) {
				int sx = int(x) + dx8[k];
				int sy = int(y) + dy8[k];
				if(sx >= 0 && sx < int(width) && sy >= 0 && sy < int(height)) {
					int si = sy*width + sx;
					//if( false == istaken[si] )//comment this to obtain internal contours
					{
						if(labels[i] != labels[si]) {
							np++;
						}
					}
				}
			}
			if(np > edge_w) {
				img(x,y,0) = edge_r;
				img(x,y,1) = edge_g;
				img(x,y,2) = edge_b;
				istaken[i] = true;
			}
		}
	}

}

}