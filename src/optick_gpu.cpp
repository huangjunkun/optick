#include "optick_gpu.h"

#if USE_OPTICK
#include "optick_core.h"
#include "optick_memory.h"

#include <thread>

namespace Optick
{
	static_assert((1ULL << 32) % GPUProfiler::MAX_QUERIES_COUNT == 0, "(1 << 32) should be a multiple of MAX_QUERIES_COUNT to handle query index overflow!");


	GPUProfiler::GPUProfiler() : currentState(STATE_OFF), currentNode(0), frameNumber(0)
	{

	}

	void GPUProfiler::InitNode(const char *nodeName, uint32_t nodeIndex)
	{
		Node* node = Memory::New<Node>();
		for (int i = 0; i < GPU_QUEUE_COUNT; ++i)
		{
			char name[128] = { 0 };
			sprintf_s(name, "%s [%s]", nodeName, GetGPUQueueName((GPUQueueType)i));
			node->gpuEventStorage[i] = RegisterStorage(name, uint64_t(-1), ThreadMask::GPU);
			node->name = nodeName;
		}
		nodes[nodeIndex] = node;
	}

	void GPUProfiler::Start(uint32 /*mode*/)
	{
		std::lock_guard<std::recursive_mutex> lock(updateLock);
		Reset();
		currentState = STATE_STARTING;
	}

	void GPUProfiler::Stop(uint32 /*mode*/)
	{
		std::lock_guard<std::recursive_mutex> lock(updateLock);
		currentState = STATE_OFF;
	}

	void GPUProfiler::Dump(uint32 /*mode*/)
	{
		for (size_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
		{
			Node* node = nodes[nodeIndex];

			for (int queueIndex = 0; queueIndex < GPU_QUEUE_COUNT; ++queueIndex)
			{
				EventBuffer& gpuBuffer = node->gpuEventStorage[queueIndex]->eventBuffer;

				const vector<ThreadEntry*>& threads = Core::Get().GetThreads();
                for (size_t threadIndex = 0; threadIndex < threads.size(); ++threadIndex)
				{
                    ThreadEntry* thread = threads[threadIndex];
					thread->storage.gpuStorage.gpuBuffer[nodeIndex][queueIndex].ForEachChunk([&gpuBuffer](const EventData* events, int count)
					{
						gpuBuffer.AddRange(events, count);
					});
				}
			}
		}
	}

	string GPUProfiler::GetName() const
	{
		return !nodes.empty() ? nodes[0]->name : string();
	}

	GPUProfiler::~GPUProfiler()
	{
		for (Node* node : nodes)
			Memory::Delete(node);
		nodes.clear();
	}

	void GPUProfiler::Reset()
	{
		for (uint32_t nodeIndex = 0; nodeIndex < nodes.size(); ++nodeIndex)
		{
			Node& node = *nodes[nodeIndex];
			node.Reset();
			node.clock = GetClockSynchronization(nodeIndex);
		}
	}

	EventData& GPUProfiler::AddFrameEvent()
	{
		static const EventDescription* GPUFrameDescription = EventDescription::Create("GPU Frame", __FILE__, __LINE__);
		EventData& event = nodes[currentNode]->gpuEventStorage[GPU_QUEUE_GRAPHICS]->eventBuffer.Add();
		event.description = GPUFrameDescription;
		event.start = EventTime::INVALID_TIMESTAMP;
		event.finish = EventTime::INVALID_TIMESTAMP;
		return event;
	}

	EventData& GPUProfiler::AddVSyncEvent()
	{
		static const EventDescription* VSyncDescription = EventDescription::Create("VSync", __FILE__, __LINE__);
		EventData& event = nodes[currentNode]->gpuEventStorage[GPU_QUEUE_VSYNC]->eventBuffer.Add();
		event.description = VSyncDescription;
		event.start = EventTime::INVALID_TIMESTAMP;
		event.finish = EventTime::INVALID_TIMESTAMP;
		return event;
	}

	TagData<uint32>& GPUProfiler::AddFrameTag()
	{
		static const EventDescription* FrameTagDescription = EventDescription::CreateShared("Frame");
		TagData<uint32>& tag = nodes[currentNode]->gpuEventStorage[GPU_QUEUE_GRAPHICS]->tagU32Buffer.Add();
		tag.description = FrameTagDescription;
		tag.timestamp = EventTime::INVALID_TIMESTAMP;
		tag.data = Core::Get().GetCurrentFrame();
		return tag;
	}

	const char * GetGPUQueueName(GPUQueueType queue)
	{
		const char* GPUQueueToName[GPU_QUEUE_COUNT] = { "Graphics", "Compute", "Transfer", "VSync" };
		return GPUQueueToName[queue];
	}

	void GPUProfiler::Node::Reset()
	{
		queryIndex = 0;

		for (size_t frameIndex = 0; frameIndex < queryGpuframes.size(); ++frameIndex)
			queryGpuframes[frameIndex].Reset();
	}
}
#endif //USE_OPTICK

