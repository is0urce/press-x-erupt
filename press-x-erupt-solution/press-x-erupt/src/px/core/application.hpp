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
		virtual void frame()
		{
			m_renderer.draw_frame();
		}

	private:
		renderer m_renderer;
	};
}