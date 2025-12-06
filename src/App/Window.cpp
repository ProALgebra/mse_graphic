#include "Window.h"

#include <QMouseEvent>
#include <QLabel>
#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QSlider>
#include <QVBoxLayout>
#include <QScreen>

#include <array>

#define TINYGLTF_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <tinygltf/tiny_gltf.h>

namespace
{

// Fullscreen quad: position (x, y) and tex coords (u, v)
constexpr std::array<GLfloat, 16u> vertices = {
	-1.0f, -1.0f, 0.0f, 0.0f,
	 1.0f, -1.0f, 1.0f, 0.0f,
	 1.0f,  1.0f, 1.0f, 1.0f,
	-1.0f,  1.0f, 0.0f, 1.0f,
};
constexpr std::array<GLuint, 6u> indices = {0, 1, 2, 0, 2, 3};

}// namespace

Window::Window() noexcept
{
	const auto formatFPS = [](const auto value) {
		return QString("FPS: %1").arg(QString::number(value));
	};

	auto fps = new QLabel(formatFPS(0), this);
	fps->setStyleSheet("QLabel { color : white; }");

	auto iterLabel = new QLabel(this);
	iterLabel->setStyleSheet("QLabel { color : white; }");
	iterLabel->setText(QString("Iterations: %1").arg(fractal_.maxIterations));

	auto iterSlider = new QSlider(Qt::Horizontal, this);
	iterSlider->setRange(50, 1000);
	iterSlider->setValue(fractal_.maxIterations);

	connect(iterSlider, &QSlider::valueChanged, this, [this, iterLabel](int value) {
		fractal_.maxIterations = value;
		iterLabel->setText(QString("Iterations: %1").arg(value));
		update();
	});

	auto intensityLabel = new QLabel(this);
	intensityLabel->setStyleSheet("QLabel { color : white; }");
	intensityLabel->setText(QString("Intensity: %1").arg(fractal_.intensity, 0, 'f', 2));

	auto intensitySlider = new QSlider(Qt::Horizontal, this);
	intensitySlider->setRange(10, 300); // 0.10 .. 3.00
	intensitySlider->setValue(static_cast<int>(fractal_.intensity * 100.0f));

	connect(intensitySlider, &QSlider::valueChanged, this, [this, intensityLabel](int value) {
		fractal_.intensity = static_cast<float>(value) / 100.0f;
		intensityLabel->setText(QString("Intensity: %1").arg(fractal_.intensity, 0, 'f', 2));
		update();
	});

	auto colorLabel = new QLabel(this);
	colorLabel->setStyleSheet("QLabel { color : white; }");
	colorLabel->setText(QString("Color shift: %1").arg(fractal_.colorShift, 0, 'f', 2));

	auto colorSlider = new QSlider(Qt::Horizontal, this);
	colorSlider->setRange(0, 628); // 0.00 .. 6.28
	colorSlider->setValue(static_cast<int>(fractal_.colorShift * 100.0f));

	connect(colorSlider, &QSlider::valueChanged, this, [this, colorLabel](int value) {
		fractal_.colorShift = static_cast<float>(value) / 100.0f;
		colorLabel->setText(QString("Color shift: %1").arg(fractal_.colorShift, 0, 'f', 2));
		update();
	});

	auto layout = new QVBoxLayout();
	layout->setAlignment(Qt::AlignLeft | Qt::AlignTop);
	layout->addWidget(fps);
	layout->addWidget(iterLabel);
	layout->addWidget(iterSlider);
	layout->addWidget(intensityLabel);
	layout->addWidget(intensitySlider);
	layout->addWidget(colorLabel);
	layout->addWidget(colorSlider);

	setLayout(layout);

	timer_.start();

	connect(this, &Window::updateUI, [=] {
		fps->setText(formatFPS(ui_.fps));
	});
}

Window::~Window()
{
	{
		// Free resources with context bounded.
		const auto guard = bindContext();
		program_.reset();
	}
}

void Window::onInit()
{
	// Configure shaders
	program_ = std::make_unique<QOpenGLShaderProgram>(this);
	program_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/Shaders/fractal.vs");
	program_->addShaderFromSourceFile(QOpenGLShader::Fragment,
									  ":/Shaders/fractal.fs");
	program_->link();

	// Create VAO object
	vao_.create();
	vao_.bind();

	// Create VBO
	vbo_.create();
	vbo_.bind();
	vbo_.setUsagePattern(QOpenGLBuffer::StaticDraw);
	vbo_.allocate(vertices.data(), static_cast<int>(vertices.size() * sizeof(GLfloat)));

	// Create IBO
	ibo_.create();
	ibo_.bind();
	ibo_.setUsagePattern(QOpenGLBuffer::StaticDraw);
	ibo_.allocate(indices.data(), static_cast<int>(indices.size() * sizeof(GLuint)));

	// Bind attributes
	program_->bind();

	program_->enableAttributeArray(0);
	program_->setAttributeBuffer(0, GL_FLOAT, 0, 2, static_cast<int>(4 * sizeof(GLfloat)));

	program_->enableAttributeArray(1);
	program_->setAttributeBuffer(1, GL_FLOAT, static_cast<int>(2 * sizeof(GLfloat)), 2,
								 static_cast<int>(4 * sizeof(GLfloat)));

	centerUniform_ = program_->uniformLocation("u_center");
	scaleUniform_ = program_->uniformLocation("u_scale");
	maxIterUniform_ = program_->uniformLocation("u_maxIter");
	aspectUniform_ = program_->uniformLocation("u_aspect");
	intensityUniform_ = program_->uniformLocation("u_intensity");
	colorShiftUniform_ = program_->uniformLocation("u_colorShift");

	// Release all
	program_->release();

	vao_.release();

	ibo_.release();
	vbo_.release();

	// Еnable depth test and face culling
	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	// Black clear color for background
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

	// Clear all FBO buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void Window::onRender()
{
	const auto guard = captureMetrics();

	// Clear buffers
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	// Bind VAO and shader program
	program_->bind();
	vao_.bind();

	// Update uniforms
	program_->setUniformValue(centerUniform_, fractal_.centerX, fractal_.centerY);
	program_->setUniformValue(scaleUniform_, fractal_.scale);
	program_->setUniformValue(maxIterUniform_, fractal_.maxIterations);
	program_->setUniformValue(aspectUniform_, aspect_);
	program_->setUniformValue(intensityUniform_, fractal_.intensity);
	program_->setUniformValue(colorShiftUniform_, fractal_.colorShift);

	// Draw
	glDrawElements(GL_TRIANGLES, static_cast<GLsizei>(indices.size()), GL_UNSIGNED_INT, nullptr);

	// Release VAO and shader program
	vao_.release();
	program_->release();

	++frameCount_;

	// Request redraw if animated
	if (animated_)
	{
		update();
	}
}

void Window::onResize(const size_t width, const size_t height)
{
	// Configure viewport
	glViewport(0, 0, static_cast<GLint>(width), static_cast<GLint>(height));

	aspect_ = static_cast<float>(width) / static_cast<float>(height ? height : 1);
}

Window::PerfomanceMetricsGuard::PerfomanceMetricsGuard(std::function<void()> callback)
	: callback_{ std::move(callback) }
{
}

Window::PerfomanceMetricsGuard::~PerfomanceMetricsGuard()
{
	if (callback_)
	{
		callback_();
	}
}

auto Window::captureMetrics() -> PerfomanceMetricsGuard
{
	return PerfomanceMetricsGuard{
		[&] {
			if (timer_.elapsed() >= 1000)
			{
				const auto elapsedSeconds = static_cast<float>(timer_.restart()) / 1000.0f;
				ui_.fps = static_cast<size_t>(std::round(frameCount_ / elapsedSeconds));
				frameCount_ = 0;
				emit updateUI();
			}
		}
	};
}

void Window::mousePressEvent(QMouseEvent * event)
{
	if (event->button() == Qt::LeftButton)
	{
		panning_ = true;
		lastMousePos_ = event->pos();
	}
}

void Window::mouseMoveEvent(QMouseEvent * event)
{
	if (!panning_)
	{
		return;
	}

	const auto pos = event->pos();
	const auto dx = pos.x() - lastMousePos_.x();
	const auto dy = pos.y() - lastMousePos_.y();
	lastMousePos_ = pos;

	const auto w = width() > 0 ? width() : 1;
	const auto h = height() > 0 ? height() : 1;

	const float uDelta = static_cast<float>(dx) / static_cast<float>(w);
	const float vDelta = static_cast<float>(-dy) / static_cast<float>(h);

	const float worldDeltaX = uDelta * fractal_.scale;
	const float worldDeltaY = vDelta * fractal_.scale / aspect_;

	fractal_.centerX -= worldDeltaX;
	fractal_.centerY -= worldDeltaY;

	update();
}

void Window::mouseReleaseEvent(QMouseEvent * event)
{
	if (event->button() == Qt::LeftButton)
	{
		panning_ = false;
	}
}

void Window::wheelEvent(QWheelEvent * event)
{
	const QPoint numDegrees = event->angleDelta() / 8;
	if (numDegrees.y() == 0)
	{
		return;
	}

	const auto w = width() > 0 ? width() : 1;
	const auto h = height() > 0 ? height() : 1;

	const auto pos = event->position();
	const float u = static_cast<float>(pos.x()) / static_cast<float>(w);
	const float v = 1.0f - static_cast<float>(pos.y()) / static_cast<float>(h);

	const float xBefore = (u - 0.5f) * fractal_.scale;
	const float yBefore = (v - 0.5f) * fractal_.scale / aspect_;
	const float cx = fractal_.centerX + xBefore;
	const float cy = fractal_.centerY + yBefore;

	const float steps = static_cast<float>(numDegrees.y()) / 15.0f;
	const float zoomFactor = std::pow(0.9f, steps);
	fractal_.scale *= zoomFactor;

	const float xAfter = (u - 0.5f) * fractal_.scale;
	const float yAfter = (v - 0.5f) * fractal_.scale / aspect_;

	fractal_.centerX = cx - xAfter;
	fractal_.centerY = cy - yAfter;

	update();
}
