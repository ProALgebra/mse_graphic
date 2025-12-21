#include "Window.h"

#include <QOpenGLFunctions>
#include <QOpenGLShaderProgram>
#include <QFile>
#include <QByteArray>
#include <QtMath>

#include <QLabel>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QSlider>
#include <QGroupBox>
#include <QFormLayout>

#include <tinygltf/tiny_gltf.h>



Window::Window() noexcept
{
	setFocusPolicy(Qt::StrongFocus);

	fpsLabel_ = new QLabel("FPS: 0", this);
	fpsLabel_->setStyleSheet("QLabel { color : white; }");

	auto morphLabel = new QLabel("Morph: 0%", this);
	morphLabel->setStyleSheet("QLabel { color : white; }");

	auto morphSlider = new QSlider(Qt::Horizontal, this);
	morphSlider->setRange(0, 100);
	morphSlider->setValue(0);

	morphSlider->setFocusPolicy(Qt::NoFocus);

	auto dirIntensitySlider = new QSlider(Qt::Horizontal, this);
	dirIntensitySlider->setRange(0, 200);
	dirIntensitySlider->setValue(
		static_cast<int>(dirLight_.intensity * 100.0f));
	dirIntensitySlider->setFocusPolicy(Qt::NoFocus);

	auto spotIntensitySlider = new QSlider(Qt::Horizontal, this);
	spotIntensitySlider->setRange(0, 200);
	spotIntensitySlider->setValue(
		static_cast<int>(spotLight_.intensity * 100.0f));
	spotIntensitySlider->setFocusPolicy(Qt::NoFocus);

	auto spotCutoffSlider = new QSlider(Qt::Horizontal, this);
	spotCutoffSlider->setRange(5, 45);
	spotCutoffSlider->setValue(static_cast<int>(spotLight_.cutoffDegrees));
	spotCutoffSlider->setFocusPolicy(Qt::NoFocus);

	auto spotOrbitSlider = new QSlider(Qt::Horizontal, this);
	spotOrbitSlider->setRange(0, 360);
	spotOrbitSlider->setValue(0);
	spotOrbitSlider->setFocusPolicy(Qt::NoFocus);

	auto controlsGroup = new QGroupBox("Controls", this);
	controlsGroup->setStyleSheet("QGroupBox, QGroupBox QLabel { color: white; }");
	controlsGroup->setFocusPolicy(Qt::NoFocus);
	auto controlsLayout = new QFormLayout();
	controlsLayout->addRow("Morph", morphSlider);
	controlsLayout->addRow("Dir intensity", dirIntensitySlider);
	controlsLayout->addRow("Spot intensity", spotIntensitySlider);
	controlsLayout->addRow("Spot cutoff", spotCutoffSlider);
	controlsLayout->addRow("Spot orbit", spotOrbitSlider);
	controlsGroup->setLayout(controlsLayout);

	auto mainLayout = new QVBoxLayout();
	mainLayout->setContentsMargins(10, 10, 10, 10);
	mainLayout->addWidget(fpsLabel_, 0);
	mainLayout->addWidget(morphLabel, 0);
	mainLayout->addWidget(controlsGroup, 0);
	mainLayout->addStretch(1);

	setLayout(mainLayout);

	connect(morphSlider, &QSlider::valueChanged, this, [=](int value) {
		morphFactor_ = static_cast<float>(value) / 100.0f;
		morphDirty_ = true;
		morphLabel->setText(QString("Morph: %1%").arg(value));
	});

	connect(dirIntensitySlider,
			&QSlider::valueChanged,
			this,
			[=](int value) {
				dirLight_.intensity = static_cast<float>(value) / 100.0f;
			});

	connect(spotIntensitySlider,
			&QSlider::valueChanged,
			this,
			[=](int value) {
				spotLight_.intensity = static_cast<float>(value) / 100.0f;
			});

	connect(spotCutoffSlider,
			&QSlider::valueChanged,
			this,
			[=](int value) {
				spotLight_.cutoffDegrees = static_cast<float>(value);
			});

	connect(spotOrbitSlider,
			&QSlider::valueChanged,
			this,
			[=](int value) {
				spotOrbitAngleDeg_ = static_cast<float>(value);
			});
}

Window::~Window()
{
	{
		const auto guard = bindContext();
		textures_.clear();
		program_.reset();
	}
}

void Window::onInit()
{
	if (!loadModel())
	{
		initialized_ = false;
		return;
	}

	// Configure shaders
	program_ = std::make_unique<QOpenGLShaderProgram>(this);
	program_->addShaderFromSourceFile(QOpenGLShader::Vertex, ":/Shaders/diffuse.vs");
	program_->addShaderFromSourceFile(QOpenGLShader::Fragment,
									  ":/Shaders/diffuse.fs");
	program_->link();

	vao_.create();
	vao_.bind();

	vbo_.create();
	vbo_.bind();
	vbo_.setUsagePattern(QOpenGLBuffer::StaticDraw);
	vbo_.allocate(static_cast<int>(vertices_.size() * sizeof(Vertex)));
	vbo_.write(0, vertices_.data(),
			   static_cast<int>(vertices_.size() * sizeof(Vertex)));

	ibo_.create();
	ibo_.bind();
	ibo_.setUsagePattern(QOpenGLBuffer::StaticDraw);
	ibo_.allocate(indices_.data(), static_cast<int>(indices_.size() * sizeof(GLuint)));

	// Создаём текстуры из glTF-изображений.
	textures_.clear();
	for (const auto & img : textureImages_)
	{
		if (img.isNull())
		{
			textures_.emplace_back();
			continue;
		}

		auto tex = std::make_unique<QOpenGLTexture>(img);
		tex->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
		tex->setWrapMode(QOpenGLTexture::WrapMode::Repeat);
		textures_.emplace_back(std::move(tex));
	}

	if (textures_.empty())
	{
		auto tex =
			std::make_unique<QOpenGLTexture>(QImage(":/Textures/voronoi.png"));
		tex->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
		tex->setWrapMode(QOpenGLTexture::WrapMode::Repeat);
		textures_.emplace_back(std::move(tex));
	}

	program_->bind();

	program_->enableAttributeArray(0);
	program_->setAttributeBuffer(
		0, GL_FLOAT, offsetof(Vertex, position), 3, static_cast<int>(sizeof(Vertex)));

	program_->enableAttributeArray(1);
	program_->setAttributeBuffer(
		1, GL_FLOAT, offsetof(Vertex, normal), 3, static_cast<int>(sizeof(Vertex)));

	program_->enableAttributeArray(2);
	program_->setAttributeBuffer(
		2, GL_FLOAT, offsetof(Vertex, texCoord), 2, static_cast<int>(sizeof(Vertex)));

	program_->enableAttributeArray(3);
	program_->setAttributeBuffer(
		3, GL_FLOAT, offsetof(Vertex, morphPosition), 3, static_cast<int>(sizeof(Vertex)));

	program_->enableAttributeArray(4);
	program_->setAttributeBuffer(
		4, GL_FLOAT, offsetof(Vertex, morphNormal), 3, static_cast<int>(sizeof(Vertex)));

	program_->setUniformValue("tex_2d", 0);

	mvpUniform_ = -1;

	program_->release();

	vao_.release();

	ibo_.release();
	vbo_.release();

	glEnable(GL_DEPTH_TEST);
	glEnable(GL_CULL_FACE);

	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	fpsTimer_.start();
	frameCount_ = 0;
	frameTimer_.start();
	lastFrameMs_ = frameTimer_.elapsed();

	initialized_ = true;
}

void Window::onRender()
{
	if (!initialized_ || !program_ || !vao_.isCreated()
		|| indices_.empty() || vertices_.empty())
	{
		return;
	}

	const qint64 now = frameTimer_.elapsed();
	const float dt = static_cast<float>(now - lastFrameMs_) / 1000.0f;
	lastFrameMs_ = now;

	updateCamera(dt);
	updateMorphGeometryIfNeeded();

	++frameCount_;
	if (fpsTimer_.elapsed() >= 1000)
	{
		const float sec = static_cast<float>(fpsTimer_.restart()) / 1000.0f;
		const float fps = frameCount_ / (sec > 0.0f ? sec : 1.0f);
		frameCount_ = 0;
		if (fpsLabel_)
		{
			fpsLabel_->setText(
				QString("FPS: %1").arg(fps, 0, 'f', 1));
		}
	}

	glClearColor(0.05f, 0.05f, 0.08f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	model_.setToIdentity();
	view_.setToIdentity();
	view_.lookAt(cameraPos_, cameraPos_ + cameraFront_, cameraUp_);
	const auto normalMatrix = model_.normalMatrix();

	program_->bind();
	vao_.bind();

	program_->setUniformValue("model", model_);
	program_->setUniformValue("view", view_);
	program_->setUniformValue("projection", projection_);
	program_->setUniformValue("normalMatrix", normalMatrix);

	program_->setUniformValue("morphFactor", morphFactor_);

	program_->setUniformValue("viewPos", cameraPos_);

	program_->setUniformValue("ambientStrength", ambientStrength_);
	program_->setUniformValue("specularStrength", specularStrength_);
	program_->setUniformValue("shininess", shininess_);

	program_->setUniformValue("dirLight.direction", dirLight_.direction);
	program_->setUniformValue("dirLight.color", dirLight_.color);
	program_->setUniformValue("dirLight.intensity", dirLight_.intensity);

	// Позиция прожектора
	{
		const float radius = 3.0f;
		const float angleRad = qDegreesToRadians(spotOrbitAngleDeg_);
		QVector3D pos(radius * qSin(angleRad), spotLight_.position.y(), radius * qCos(angleRad));
		spotLight_.position = pos;
		const QVector3D target(0.0f, 0.0f, 0.0f);
		spotLight_.direction = (target - pos);
	}

	const float cutoffRadians = qDegreesToRadians(spotLight_.cutoffDegrees);
	const float cutoffCos = qCos(cutoffRadians);
	const float outerCutoffCos =
		qCos(cutoffRadians + qDegreesToRadians(5.0f));

	program_->setUniformValue("spotLight.position", spotLight_.position);
	program_->setUniformValue("spotLight.direction",
							  spotLight_.direction.normalized());
	program_->setUniformValue("spotLight.color", spotLight_.color);
	program_->setUniformValue("spotLight.intensity", spotLight_.intensity);
	program_->setUniformValue("spotLight.cutoff", cutoffCos);
	program_->setUniformValue("spotLight.outerCutoff", outerCutoffCos);

	glActiveTexture(GL_TEXTURE0);

	if (submeshes_.empty())
	{
		const int texIdx = !textures_.empty() && textures_.front()
							   ? 0
							   : -1;
		if (texIdx >= 0)
		{
			textures_[texIdx]->bind();
		}

		glDrawElements(GL_TRIANGLES,
					   static_cast<GLsizei>(indices_.size()),
					   GL_UNSIGNED_INT,
					   nullptr);

		if (texIdx >= 0)
		{
			textures_[texIdx]->release();
		}
	}
	else
	{
		for (const auto & sm : submeshes_)
		{
			int texIdx = sm.textureIndex;
			if (texIdx < 0
				|| texIdx >= static_cast<int>(textures_.size())
				|| !textures_[texIdx])
			{
				texIdx = (!textures_.empty() && textures_.front()) ? 0 : -1;
			}

			if (texIdx >= 0)
			{
				textures_[texIdx]->bind();
			}

			glDrawElements(GL_TRIANGLES,
						   sm.indexCount,
						   GL_UNSIGNED_INT,
						   reinterpret_cast<const void *>(
							   static_cast<size_t>(sm.indexOffset)
							   * sizeof(GLuint)));

			if (texIdx >= 0)
			{
				textures_[texIdx]->release();
			}
		}
	}
	vao_.release();
	program_->release();
	update();
}

void Window::onResize(const size_t width, const size_t height)
{
	updateProjection(width, height);
}

void Window::updateProjection(const size_t width, const size_t height)
{
	const auto w = static_cast<GLint>(width);
	const auto h = static_cast<GLint>(height ? height : 1);

	glViewport(0, 0, w, h);

	const auto aspect = static_cast<float>(w) / static_cast<float>(h);
	const auto zNear = 0.1f;
	const auto zFar = 100.0f;

	projection_.setToIdentity();
	projection_.perspective(fov_, aspect, zNear, zFar);
}

void Window::updateCamera(float dt)
{
	if (dt <= 0.0f)
	{
		return;
	}

	const float velocity = cameraSpeed_ * dt;

	if (pressedKeys_.contains(Qt::Key_W))
	{
		cameraPos_ += cameraFront_ * velocity;
	}
	if (pressedKeys_.contains(Qt::Key_S))
	{
		cameraPos_ -= cameraFront_ * velocity;
	}
	if (pressedKeys_.contains(Qt::Key_A))
	{
		cameraPos_ -= QVector3D::crossProduct(cameraFront_, cameraUp_).normalized()
					  * velocity;
	}
	if (pressedKeys_.contains(Qt::Key_D))
	{
		cameraPos_ += QVector3D::crossProduct(cameraFront_, cameraUp_).normalized()
					  * velocity;
	}
	if (pressedKeys_.contains(Qt::Key_Q))
	{
		cameraPos_ -= cameraUp_ * velocity;
	}
	if (pressedKeys_.contains(Qt::Key_E))
	{
		cameraPos_ += cameraUp_ * velocity;
	}
}

void Window::updateCameraVectors()
{
	const float yawRad = qDegreesToRadians(yaw_);
	const float pitchRad = qDegreesToRadians(pitch_);

	QVector3D front;
	front.setX(qCos(yawRad) * qCos(pitchRad));
	front.setY(qSin(pitchRad));
	front.setZ(qSin(yawRad) * qCos(pitchRad));
	cameraFront_ = front.normalized();
}

void Window::updateMorphGeometryIfNeeded()
{
	
}

bool Window::loadModel()
{
	QFile file(":/Models/cube.glb");

	if (!file.open(QIODevice::ReadOnly))
	{
		qWarning("Модель не открылась");
		return false;
	}

	const QByteArray data = file.readAll();

	tinygltf::TinyGLTF loader;
	tinygltf::Model model;
	std::string err;
	std::string warn;

	const auto success = loader.LoadBinaryFromMemory(
		&model,
		&err,
		&warn,
		reinterpret_cast<const unsigned char *>(data.constData()),
		static_cast<unsigned int>(data.size()));

	textureImages_.clear();
	textureImages_.resize(model.textures.size());
	for (size_t ti = 0; ti < model.textures.size(); ++ti)
	{
		const auto & tex = model.textures[ti];
		if (tex.source < 0
			|| tex.source >= static_cast<int>(model.images.size()))
		{
			continue;
		}

		const auto & img = model.images[static_cast<size_t>(tex.source)];
		if (img.width <= 0 || img.height <= 0 || img.image.empty())
		{
			continue;
		}

		QImage::Format fmt = QImage::Format_RGBA8888;
		if (img.component == 3)
		{
			fmt = QImage::Format_RGB888;
		}
		else if (img.component == 4)
		{
			fmt = QImage::Format_RGBA8888;
		}

		QImage qimg(img.width, img.height, fmt);
		const int srcStride = img.component * img.width * (img.bits / 8);

		for (int y = 0; y < img.height; ++y)
		{
			const auto * srcRow = img.image.data() + srcStride * y;
			std::memcpy(qimg.scanLine(y), srcRow, srcStride);
		}

		textureImages_[ti] = qimg;
	}



	std::vector<Vertex> vertices;
	std::vector<GLuint> indices;
	std::vector<Submesh> submeshes;

	const auto readVec3Accessor =
		[&](int accessorIndex, std::vector<QVector3D> & out) -> bool
		{


			const auto & accessor =
				model.accessors[static_cast<size_t>(accessorIndex)];



			const auto & view =
				model.bufferViews[static_cast<size_t>(accessor.bufferView)];


			const auto & buffer =
				model.buffers[static_cast<size_t>(view.buffer)];
			const auto stride = accessor.ByteStride(view);

			const auto * base =
				buffer.data.data() + view.byteOffset + accessor.byteOffset;

			out.resize(accessor.count);
			for (size_t i = 0; i < accessor.count; ++i)
			{
				const auto * ptr =
					reinterpret_cast<const float *>(base + stride * i);
				out[i] = QVector3D(ptr[0], ptr[1], ptr[2]);
			}

			return true;
		};

	const auto processMeshPrimitive =
		[&](const tinygltf::Primitive & primitive,
			const QMatrix4x4 & modelMat)
		{
			const auto posIt = primitive.attributes.find("POSITION");
			if (posIt == primitive.attributes.end())
			{
				return;
			}

			const auto normalIt = primitive.attributes.find("NORMAL");
			const auto texIt = primitive.attributes.find("TEXCOORD_0");

			int materialTexIndex = -1;
			if (primitive.material >= 0
				&& primitive.material
					   < static_cast<int>(model.materials.size()))
			{
				const auto & mat =
					model.materials[static_cast<size_t>(primitive.material)];
				const int texIndex =
					mat.pbrMetallicRoughness.baseColorTexture.index;
				if (texIndex >= 0
					&& texIndex < static_cast<int>(model.textures.size()))
				{
					materialTexIndex = texIndex;
				}
			}

			if (posIt->second < 0
				|| posIt->second >= static_cast<int>(model.accessors.size()))
			{
				return;
			}

			const auto & posAccessor =
				model.accessors[static_cast<size_t>(posIt->second)];

			if (posAccessor.bufferView < 0
				|| posAccessor.bufferView
					   >= static_cast<int>(model.bufferViews.size()))
			{
				return;
			}

			const auto & posView =
				model.bufferViews[static_cast<size_t>(posAccessor.bufferView)];
			if (posView.buffer < 0
				|| posView.buffer >= static_cast<int>(model.buffers.size()))
			{
				return;
			}

			const auto & posBuffer =
				model.buffers[static_cast<size_t>(posView.buffer)];
			const auto posStride = posAccessor.ByteStride(posView);
			if (posStride <= 0)
			{
				return;
			}

			const auto * posBase =
				posBuffer.data.data() + posView.byteOffset + posAccessor.byteOffset;

			std::vector<QVector3D> positions(posAccessor.count);
			for (size_t i = 0; i < posAccessor.count; ++i)
			{
				const auto * ptr =
					reinterpret_cast<const float *>(posBase + i * posStride);
				positions[i] = QVector3D(ptr[0], ptr[1], ptr[2]);
			}

			std::vector<QVector3D> normals;
			if (normalIt != primitive.attributes.end()
				&& normalIt->second >= 0
				&& normalIt->second < static_cast<int>(model.accessors.size()))
			{
				const auto & normalAccessor =
					model.accessors[static_cast<size_t>(normalIt->second)];
				if (normalAccessor.bufferView >= 0
					&& normalAccessor.bufferView
						   < static_cast<int>(model.bufferViews.size()))
				{
					const auto & normalView = model.bufferViews[static_cast<size_t>(
						normalAccessor.bufferView)];
					if (normalView.buffer >= 0
						&& normalView.buffer
							   < static_cast<int>(model.buffers.size()))
					{
						const auto & normalBuffer =
							model.buffers[static_cast<size_t>(normalView.buffer)];
						const auto normalStride =
							normalAccessor.ByteStride(normalView);
						if (normalStride > 0)
						{
							const auto * normalBase =
								normalBuffer.data.data() + normalView.byteOffset
								+ normalAccessor.byteOffset;

							normals.resize(normalAccessor.count);
							for (size_t i = 0; i < normalAccessor.count; ++i)
							{
								const auto * ptr =
									reinterpret_cast<const float *>(
										normalBase + i * normalStride);
								normals[i] =
									QVector3D(ptr[0], ptr[1], ptr[2]);
							}
						}
					}
				}
			}

			std::vector<QVector2D> texcoords;
			if (texIt != primitive.attributes.end()
				&& texIt->second >= 0
				&& texIt->second < static_cast<int>(model.accessors.size()))
			{
				const auto & texAccessor =
					model.accessors[static_cast<size_t>(texIt->second)];
				if (texAccessor.bufferView >= 0
					&& texAccessor.bufferView
						   < static_cast<int>(model.bufferViews.size()))
				{
					const auto & texView =
						model.bufferViews[static_cast<size_t>(texAccessor.bufferView)];
					if (texView.buffer >= 0
						&& texView.buffer
							   < static_cast<int>(model.buffers.size()))
					{
						const auto & texBuffer =
							model.buffers[static_cast<size_t>(texView.buffer)];
						const auto texStride =
							texAccessor.ByteStride(texView);
						if (texStride > 0)
						{
							const auto * texBase =
								texBuffer.data.data() + texView.byteOffset
								+ texAccessor.byteOffset;

							texcoords.resize(texAccessor.count);
							for (size_t i = 0; i < texAccessor.count; ++i)
							{
								const auto * ptr =
									reinterpret_cast<const float *>(
										texBase + i * texStride);
								texcoords[i] =
									QVector2D(ptr[0], ptr[1]);
							}
						}
					}
				}
			}

			if (primitive.indices < 0
				|| primitive.indices >= static_cast<int>(model.accessors.size()))
			{
				return;
			}

			const auto & indexAccessor =
				model.accessors[static_cast<size_t>(primitive.indices)];
			if (indexAccessor.bufferView < 0
				|| indexAccessor.bufferView
					   >= static_cast<int>(model.bufferViews.size()))
			{
				return;
			}

			const auto & indexView =
				model.bufferViews[static_cast<size_t>(indexAccessor.bufferView)];
			if (indexView.buffer < 0
				|| indexView.buffer >= static_cast<int>(model.buffers.size()))
			{
				return;
			}

			const auto & indexBuffer =
				model.buffers[static_cast<size_t>(indexView.buffer)];

			const auto rawStride = indexAccessor.ByteStride(indexView);
			const auto indexStride =
				rawStride > 0
					? rawStride
					: tinygltf::GetComponentSizeInBytes(
						static_cast<uint32_t>(indexAccessor.componentType));
			if (indexStride <= 0)
			{
				return;
			}

			const auto * indexBase = indexBuffer.data.data()
									 + indexView.byteOffset
									 + indexAccessor.byteOffset;

			const size_t baseVertex = vertices.size();
			for (size_t i = 0; i < positions.size(); ++i)
			{
				Vertex v{};
				const auto worldPos = modelMat * positions[i];
				v.position = worldPos;

				if (!normals.empty() && i < normals.size())
				{
					auto n = modelMat.mapVector(normals[i]);
					if (!n.isNull())
					{
						n.normalize();
					}
					v.normal = n;
				}
				else
				{
					v.normal = QVector3D(0.0f, 1.0f, 0.0f);
				}

				if (!texcoords.empty() && i < texcoords.size())
				{
					v.texCoord = texcoords[i];
				}
				else
				{
					v.texCoord = QVector2D(0.0f, 0.0f);
				}

				vertices.push_back(v);
			}

			const size_t indexCountBefore = indices.size();
			indices.resize(indexCountBefore + indexAccessor.count);
			for (size_t i = 0; i < indexAccessor.count; ++i)
			{
				const auto * ptr = indexBase + i * indexStride;
				GLuint idx = 0;
				switch (indexAccessor.componentType)
				{
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_SHORT:
					idx = static_cast<GLuint>(
						*reinterpret_cast<const uint16_t *>(ptr));
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_INT:
					idx = static_cast<GLuint>(
						*reinterpret_cast<const uint32_t *>(ptr));
					break;
				case TINYGLTF_COMPONENT_TYPE_UNSIGNED_BYTE:
					idx = static_cast<GLuint>(
						*reinterpret_cast<const uint8_t *>(ptr));
					break;
				default:
					return;
				}
				indices[indexCountBefore + i] = static_cast<GLuint>(
					baseVertex + static_cast<size_t>(idx));
			}

			Submesh sm;
			sm.indexOffset = static_cast<GLsizei>(indexCountBefore);
			sm.indexCount = static_cast<GLsizei>(indexAccessor.count);
			sm.textureIndex = materialTexIndex;
			submeshes.push_back(sm);
		};

	std::function<void(int, const QMatrix4x4 &)> processNode;
	processNode = [&](int nodeIndex, const QMatrix4x4 & parentMat)
	{
		if (nodeIndex < 0
			|| nodeIndex >= static_cast<int>(model.nodes.size()))
		{
			return;
		}

		const auto & node = model.nodes[static_cast<size_t>(nodeIndex)];

		QMatrix4x4 local;
		local.setToIdentity();

		if (!node.matrix.empty() && node.matrix.size() == 16)
		{
			float m[16];
			for (int i = 0; i < 16; ++i)
			{
				m[i] = static_cast<float>(node.matrix[static_cast<size_t>(i)]);
			}
			local = QMatrix4x4(m);
		}
		else
		{
			if (!node.translation.empty() && node.translation.size() == 3)
			{
				local.translate(static_cast<float>(node.translation[0]),
								static_cast<float>(node.translation[1]),
								static_cast<float>(node.translation[2]));
			}

			if (!node.rotation.empty() && node.rotation.size() == 4)
			{
				const QQuaternion q(
					static_cast<float>(node.rotation[3]),
					static_cast<float>(node.rotation[0]),
					static_cast<float>(node.rotation[1]),
					static_cast<float>(node.rotation[2]));
				local.rotate(q);
			}

			if (!node.scale.empty() && node.scale.size() == 3)
			{
				local.scale(static_cast<float>(node.scale[0]),
							static_cast<float>(node.scale[1]),
							static_cast<float>(node.scale[2]));
			}
		}

		const QMatrix4x4 world = parentMat * local;

		if (node.mesh >= 0
			&& node.mesh < static_cast<int>(model.meshes.size()))
		{
			const auto & mesh =
				model.meshes[static_cast<size_t>(node.mesh)];

			const auto extIt =
				node.extensions.find("EXT_mesh_gpu_instancing");
			if (extIt != node.extensions.end() && extIt->second.IsObject())
			{
				const auto & extObj = extIt->second;
				const auto & attrsVal = extObj.Get("attributes");
				if (attrsVal.IsObject())
				{
					const auto & trVal = attrsVal.Get("TRANSLATION");
					const auto & scVal = attrsVal.Get("SCALE");

					int trAccessor = -1;
					int scAccessor = -1;

					if (trVal.IsNumber())
					{
						trAccessor = trVal.GetNumberAsInt();
					}
					if (scVal.IsNumber())
					{
						scAccessor = scVal.GetNumberAsInt();
					}

					std::vector<QVector3D> translations;
					std::vector<QVector3D> scales;

					if (trAccessor >= 0)
					{
						readVec3Accessor(trAccessor, translations);
					}
					if (scAccessor >= 0)
					{
						readVec3Accessor(scAccessor, scales);
					}

					const size_t instCount =
						!translations.empty()
							? translations.size()
							: (!scales.empty() ? scales.size() : 0);

					if (instCount > 0)
					{
						for (size_t i = 0; i < instCount; ++i)
						{
							QMatrix4x4 inst;
							inst.setToIdentity();

							if (!translations.empty()
								&& i < translations.size())
							{
								const auto & t = translations[i];
								inst.translate(t);
							}
							if (!scales.empty() && i < scales.size())
							{
								const auto & s = scales[i];
								inst.scale(s.x(), s.y(), s.z());
							}

							const QMatrix4x4 total = world * inst;
							for (const auto & primitive : mesh.primitives)
							{
								processMeshPrimitive(primitive, total);
							}
						}
					}
					else
					{
						for (const auto & primitive : mesh.primitives)
						{
							processMeshPrimitive(primitive, world);
						}
					}
				}
				else
				{
					for (const auto & primitive : mesh.primitives)
					{
						processMeshPrimitive(primitive, world);
					}
				}
			}
			else
			{
				for (const auto & primitive : mesh.primitives)
				{
					processMeshPrimitive(primitive, world);
				}
			}
		}

		for (const int childIndex : node.children)
		{
			processNode(childIndex, world);
		}
	};

	QMatrix4x4 rootMat;
	rootMat.setToIdentity();

	if (!model.scenes.empty())
	{
		const int sceneIndex =
			(model.defaultScene >= 0
				 && model.defaultScene < static_cast<int>(model.scenes.size()))
				? model.defaultScene
				: 0;
		const auto & scene =
			model.scenes[static_cast<size_t>(sceneIndex)];
		for (const int nodeIndex : scene.nodes)
		{
			processNode(nodeIndex, rootMat);
		}
	}
	else
	{
		for (int i = 0; i < static_cast<int>(model.nodes.size()); ++i)
		{
			processNode(i, rootMat);
		}
	}


	baseVertices_ = std::move(vertices);
	indices_ = std::move(indices);
	morphVertices_.resize(baseVertices_.size());

	QVector3D center(0.0f, 0.0f, 0.0f);
	for (const auto & v : baseVertices_)
	{
		center += v.position;
	}
	center /= static_cast<float>(baseVertices_.size());

	float maxRadius = 0.0f;
	for (const auto & v : baseVertices_)
	{
		const float r = (v.position - center).length();
		if (r > maxRadius)
		{
			maxRadius = r;
		}
	}
	if (maxRadius <= 0.0f)
	{
		maxRadius = 1.0f;
	}
	{
		const float distance = maxRadius * 2.5f;
		const float height = maxRadius * 0.3f;
		cameraPos_ = center + QVector3D(0.0f, height, distance);
	}

	for (size_t i = 0; i < baseVertices_.size(); ++i)
	{
		const auto & src = baseVertices_[i];
		Vertex dst{};

		const QVector3D dir = (src.position - center).normalized();
		dst.position = center + dir * maxRadius;
		dst.normal = dir.isNull() ? QVector3D(0.0f, 1.0f, 0.0f) : dir;
		dst.texCoord = src.texCoord;

		morphVertices_[i] = dst;
	}
	vertices_.resize(baseVertices_.size());
	for (size_t i = 0; i < baseVertices_.size(); ++i)
	{
		vertices_[i].position = baseVertices_[i].position;
		vertices_[i].normal = baseVertices_[i].normal;
		vertices_[i].texCoord = baseVertices_[i].texCoord;
		vertices_[i].morphPosition = morphVertices_[i].position;
		vertices_[i].morphNormal = morphVertices_[i].normal;
	}

	morphFactor_ = 0.0f;
	morphDirty_ = false;

	submeshes_ = std::move(submeshes);

	return true;
}

void Window::keyPressEvent(QKeyEvent * event)
{
	pressedKeys_.insert(event->key());
	fgl::GLWidget::keyPressEvent(event);
}

void Window::keyReleaseEvent(QKeyEvent * event)
{
	pressedKeys_.remove(event->key());
	fgl::GLWidget::keyReleaseEvent(event);
}

void Window::mousePressEvent(QMouseEvent * event)
{
	if (event->button() == Qt::RightButton)
	{
		rightButtonPressed_ = true;
		lastMousePos_ = event->pos();
	}

	fgl::GLWidget::mousePressEvent(event);
}

void Window::mouseReleaseEvent(QMouseEvent * event)
{
	if (event->button() == Qt::RightButton)
	{
		rightButtonPressed_ = false;
	}

	fgl::GLWidget::mouseReleaseEvent(event);
}

void Window::mouseMoveEvent(QMouseEvent * event)
{
	if (rightButtonPressed_)
	{
		const QPoint delta = event->pos() - lastMousePos_;
		lastMousePos_ = event->pos();

		yaw_ += static_cast<float>(delta.x()) * mouseSensitivity_;
		pitch_ -= static_cast<float>(delta.y()) * mouseSensitivity_;

		updateCameraVectors();
	}

	fgl::GLWidget::mouseMoveEvent(event);
}

void Window::wheelEvent(QWheelEvent * event)
{
	const auto numDegrees = event->angleDelta().y() / 8.0;
	const auto numSteps = numDegrees / 15.0;
	fov_ -= static_cast<float>(numSteps) * 2.0f;
	if (fov_ < 1.0f)
	{
		fov_ = 1.0f;
	}
	if (fov_ > 175.0f)
	{
		fov_ = 175.0f;
	}

	updateProjection(static_cast<size_t>(width()),
					 static_cast<size_t>(height()));

	fgl::GLWidget::wheelEvent(event);
}
