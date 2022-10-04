#include "cepch.h"
#include "RendererAPI.h"


struct GLFWwindow;

namespace Chonps
{
	static API s_API = API::None;

	bool renderInit(API api /*= API::OpenGL*/)
	{
		if (api == API::None)
		{
			s_API = api;
			CHONPS_CORE_WARN("No graphics API selected!");
			return false;
		}
		s_API = api;
		CHONPS_CORE_INFO("Rendering Context Initialized: {0}", getGraphicsContextName());
		return true;
	}

	API getGraphicsContext()
	{
		return s_API;
	}

	std::string getGraphicsContextName()
	{
		switch (getGraphicsContext())
		{
			case Chonps::API::None:
			{
				return "None";
				break;
			}
			case Chonps::API::OpenGL:
			{
				return "OpenGL";
				break;
			}
			case Chonps::API::Vulkan:
			{
				return "Vulkan";
				break;
			}
			case Chonps::API::DirectX:
			{
				return "DirectX";
				break;
			}
			default:
			{
				CHONPS_CORE_ERROR("Cannot find the graphics API selected!");
				break;
			}
		}
		return "null";
	}
}