#include "WdgtDasvVis.h"
#include <Slimage/IO.hpp>
#include <QtGui/QMdiSubWindow>
#include <boost/bind.hpp>

void PrepareEngine(const boost::shared_ptr<Candy::Engine>& engine)
{
	// auto view_ = engine->getView();
	auto scene = engine->getScene();

	// boost::shared_ptr<Candy::DirectionalLight> light1(new Candy::DirectionalLight(Danvil::ctLinAlg::Vec3f(+1.0f, +1.0f, -1.0f)));
	// light1->setDiffuse(Danvil::Colorf(1.0f, 1.0f, 1.0f));
	// scene->addLight(light1);
	// boost::shared_ptr<Candy::DirectionalLight> light2(new Candy::DirectionalLight(Danvil::ctLinAlg::Vec3f(-1.0f, -1.0f, -1.0f)));
	// light2->setDiffuse(Danvil::Colorf(1.0f, 1.0f, 1.0f));
	// scene->addLight(light2);

	engine->setClearColor(Danvil::Color::Grey);

	scene->setShowCoordinateCross(true);

}

WdgtDasvVis::WdgtDasvVis(QWidget *parent)
    : QMainWindow(parent)
{
	ui.setupUi(this);

	std::cout << "Preparing OpenGL widgets ..." << std::endl;

	widget_candy_global_ = new Candy::GLSystemQtWindow(0);
	auto w1 = ui.mdiArea->addSubWindow(widget_candy_global_);
	w1->setWindowTitle("3D global");
	engine_global_ = widget_candy_global_->getEngine();
	PrepareEngine(engine_global_);

	widget_candy_frame_ = new Candy::GLSystemQtWindow(0);
	auto w2 = ui.mdiArea->addSubWindow(widget_candy_frame_);
	w2->setWindowTitle("3D frame");
	engine_frame_ = widget_candy_frame_->getEngine();
	PrepareEngine(engine_frame_);

	QObject::connect(&timer_tick_, SIGNAL(timeout()), this, SLOT(tick()));
	timer_tick_.setInterval(1);
	timer_tick_.start();

	dasv::DebugSetDisplayImageCallback(boost::bind(&WdgtDasvVis::showImage, this, _1, _2));
}

WdgtDasvVis::~WdgtDasvVis()
{
}

void WdgtDasvVis::setRgbdStream(const std::shared_ptr<RgbdStream>& rgbd_stream)
{
	rgbd_stream_ = rgbd_stream;
	dasv_ = std::make_shared<dasv::ContinuousSupervoxels>();
	dasv_->start();
}

void WdgtDasvVis::tick()
{
	if(rgbd_stream_->grab()) {
		Rgbd data = rgbd_stream_->get();
		showImage("color", data.color);
		// slimage::gui::Show("depth", data.depth, 500, 3000, 0);
		dasv_->step(data.color, data.depth);
	}
}

void WdgtDasvVis::showImage(const std::string& tag, const slimage::Image3ub& img)
{
	// convert to Qt image
	QImage* qimg = slimage::qt::ConvertToQt(img);
	if(qimg == 0) {
		return;
	}
	// prepare subwindow list
	std::map<std::string, QMdiSubWindow*> subwindows_by_tag;
	for(QMdiSubWindow* w : ui.mdiArea->subWindowList()) {
		subwindows_by_tag[w->windowTitle().toStdString()] = w;
	}
	// find label
	auto it = subwindows_by_tag.find(tag);
	// create new if none exists
	QMdiSubWindow* sw;
	QLabel* qlabel;
	if(it != subwindows_by_tag.end()) {
		sw = it->second;
		qlabel = (QLabel*)sw->widget();
	} else {
		qlabel = new QLabel();
		subwindows_by_tag[tag] = sw;
		sw = ui.mdiArea->addSubWindow(qlabel);
		sw->setWindowTitle(QString::fromStdString(tag));
		sw->show();
	}
	// display image in label
	qlabel->setPixmap(QPixmap::fromImage(*qimg));
	qlabel->adjustSize();
	sw->adjustSize();
	// cleanup
	delete qimg;
}
