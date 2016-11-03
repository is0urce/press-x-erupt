#pragma once

#include "basic_application.hpp"

#include <px/renderer.hpp>

namespace px
{
	class application;
	class application : public basic_application
	{
	public:
		application()
			: basic_application{"press-x"}
			, m_renderer(*this)
		{
		}

		virtual ~application()
		{
		}

	protected:
		virtual void frame() override
		{
			m_renderer.draw_frame();
		}
		virtual void on_resize(int width, int height) override
		{
			m_renderer.resize(width, height);
		}

	private:
		renderer m_renderer;
	};
}