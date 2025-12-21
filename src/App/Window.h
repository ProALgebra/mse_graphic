#pragma once

#include <Base/GLWidget.hpp>

#include <QElapsedTimer>
#include <QMatrix4x4>
#include <QOpenGLBuffer>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLVertexArrayObject>
#include <QImage>
#include <QVector2D>
#include <QVector3D>

#include <functional>
#include <memory>
#include <vector>
#include <QSet>
#include <QPoint>

class QKeyEvent;
class QMouseEvent;
class QWheelEvent;
class QLabel;

class Window : public fgl::GLWidget
{
	Q_OBJECT
public:
	Window() noexcept;
	~Window() override;

	void onInit() override;
	void onRender() override;
	void onResize(size_t width, size_t height) override;

protected:
	void keyPressEvent(QKeyEvent * event) override;
	void keyReleaseEvent(QKeyEvent * event) override;
	void mousePressEvent(QMouseEvent * event) override;
	void mouseReleaseEvent(QMouseEvent * event) override;
	void mouseMoveEvent(QMouseEvent * event) override;
	void wheelEvent(QWheelEvent * event) override;

private:
	struct Vertex
	{

		QVector3D position;
		QVector3D normal;
		QVector2D texCoord;

		QVector3D morphPosition;
		QVector3D morphNormal;
	};

	struct DirectionalLight
	{
		QVector3D direction = QVector3D(-0.3f, -1.0f, -0.3f);
		QVector3D color = QVector3D(1.0f, 1.0f, 1.0f);
		float intensity = 1.0f;
	};

	struct SpotLight
	{
		QVector3D position = QVector3D(0.0f, 3.0f, 3.0f);
		QVector3D direction = QVector3D(0.0f, -1.0f, -1.0f);
		QVector3D color = QVector3D(1.0f, 1.0f, 1.0f);
		float intensity = 1.0f;
		float cutoffDegrees = 20.0f;
	};

	struct Submesh
	{
		GLsizei indexCount = 0;
		GLsizei indexOffset = 0;
		int textureIndex = -1;
	};

	GLint mvpUniform_ = -1;

	QOpenGLBuffer vbo_{QOpenGLBuffer::Type::VertexBuffer};
	QOpenGLBuffer ibo_{QOpenGLBuffer::Type::IndexBuffer};
	QOpenGLVertexArrayObject vao_;

	QMatrix4x4 model_;
	QMatrix4x4 view_;
	QMatrix4x4 projection_;

	std::vector<std::unique_ptr<QOpenGLTexture>> textures_;
	std::unique_ptr<QOpenGLShaderProgram> program_;
	QImage baseColorImage_;
	std::vector<QImage> textureImages_;

	QElapsedTimer fpsTimer_;
	size_t frameCount_ = 0;

	QLabel * fpsLabel_ = nullptr;

	QVector3D cameraPos_{0.0f, 0.7f, 2.0f};
	QVector3D cameraFront_{0.0f, 0.0f, -1.0f};
	QVector3D cameraUp_{0.0f, 1.0f, 0.0f};

	float yaw_ = -90.0f;
	float pitch_ = 0.0f;

	float cameraSpeed_ = 3.0f;
	float mouseSensitivity_ = 0.1f;
	float fov_ = 60.0f;

	float spotOrbitAngleDeg_ = 0.0f;

	bool rightButtonPressed_ = false;
	QPoint lastMousePos_;
	QSet<int> pressedKeys_;
	QElapsedTimer frameTimer_;
	qint64 lastFrameMs_ = 0;

	std::vector<Vertex> baseVertices_;
	std::vector<Vertex> morphVertices_;
	std::vector<Vertex> vertices_;
	std::vector<GLuint> indices_;
	std::vector<Submesh> submeshes_;

	float morphFactor_ = 0.0f;
	bool morphDirty_ = false;

	DirectionalLight dirLight_;
	SpotLight spotLight_;

	float ambientStrength_ = 0.15f;
	float specularStrength_ = 0.5f;
	float shininess_ = 32.0f;

	bool initialized_ = false;

	void updateProjection(size_t width, size_t height);
	void updateCamera(float dt);
	void updateCameraVectors();
	void updateMorphGeometryIfNeeded();
	bool loadModel();
};
