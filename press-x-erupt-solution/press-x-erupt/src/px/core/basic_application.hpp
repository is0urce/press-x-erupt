#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include <string>

namespace px
{
	class basic_application
	{
	public:
		void start()
		{
			glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // no opengl, so we can assign vulkan
			glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE); // static size window

			m_window = glfwCreateWindow(m_width, m_height, m_name.c_str(), nullptr, nullptr);

			glfwSetWindowUserPointer(m_window, this);
			glfwSetWindowSizeCallback(m_window, resize_callback);
		}
		static void resize_callback(GLFWwindow* window, int width, int height)
		{
			reinterpret_cast<basic_application*>(glfwGetWindowUserPointer(window))->on_resize(width, height);
		}
		int run()
		{
			main_loop();

			return 0; // no errors
		}
		int width() const noexcept
		{
			return m_width;
		}
		int height() const noexcept
		{
			return m_height;
		}
		bool fullscreen() const noexcept
		{
			return m_fullscreen;
		}
		std::string name() const noexcept
		{
			return m_name;
		}
		GLFWwindow* window() noexcept
		{
			return m_window;
		}

	public:
		basic_application(std::string name)
			: m_init(false)
			, m_fullscreen(false)
			, m_name(name)
			, m_width(800), m_height(600)
		{
			m_init = glfwInit() == GLFW_TRUE;
			if (m_init)
			{
				start();
			}
		}
		basic_application(basic_application const&) = delete;
		basic_application& operator=(basic_application const&) = delete;
		virtual ~basic_application()
		{
			glfwDestroyWindow(m_window);
			if (m_init)
			{
				glfwTerminate();
			}
		}

	protected:
		virtual void frame()
		{
		}
		virtual void on_resize(int /*width*/, int /*height*/)
		{
		}

	private:
		void main_loop()
		{
			while (!glfwWindowShouldClose(m_window))
			{
				glfwPollEvents();
				frame();
			}
		}

	private:
		bool m_init;
		bool m_fullscreen;
		int m_width;
		int m_height;
		std::string m_name;
		GLFWwindow* m_window;
	};
}