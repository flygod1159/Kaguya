#pragma once
#include <Core/CriticalSection.h>
#include "D3D12Common.h"
#include "CommandList.h"

struct CommandListBatch
{
	enum
	{
		BatchLimit = 64
	};

	CommandListBatch()
	{
		std::memset(CommandLists, NULL, sizeof(CommandLists));
		NumCommandLists = 0;
	}

	void Add(ID3D12CommandList* CommandList)
	{
		CommandLists[NumCommandLists] = CommandList;
		NumCommandLists++;
	}

	ID3D12CommandList* CommandLists[BatchLimit];
	UINT			   NumCommandLists;
};

class CommandAllocatorPool : public DeviceChild
{
public:
	CommandAllocatorPool(Device* Parent, D3D12_COMMAND_LIST_TYPE CommandListType) noexcept;

	CommandAllocator* RequestCommandAllocator();

	void DiscardCommandAllocator(CommandAllocator* CommandAllocator);

private:
	const D3D12_COMMAND_LIST_TYPE CommandListType;

	std::vector<std::unique_ptr<CommandAllocator>> CommandAllocators;
	std::queue<CommandAllocator*>				   CommandAllocatorQueue;
	CriticalSection								   CriticalSection;
};

class CommandQueue : public DeviceChild
{
public:
	CommandQueue(Device* Parent, D3D12_COMMAND_LIST_TYPE CommandListType) noexcept;

	void Initialize(ECommandQueueType CommandQueueType, UINT NumCommandLists = 1);

	[[nodiscard]] ID3D12CommandQueue* GetCommandQueue() const { return pCommandQueue.Get(); }
	[[nodiscard]] UINT64			  GetFrequency() const { return Frequency; }
	[[nodiscard]] UINT64			  GetCompletedValue() const { return Fence->GetCompletedValue(); }

	[[nodiscard]] UINT64 AdvanceGpu();

	[[nodiscard]] bool IsFenceComplete(UINT64 FenceValue) const;

	void WaitForFence(UINT64 FenceValue);

	void Wait(CommandQueue* CommandQueue);
	void WaitForSyncPoint(const CommandSyncPoint& SyncPoint);

	void Flush() { WaitForFence(AdvanceGpu()); }

	[[nodiscard]] CommandListHandle RequestCommandList(CommandAllocator* CommandAllocator)
	{
		CommandListHandle Handle;
		if (!AvailableCommandListHandles.empty())
		{
			Handle = AvailableCommandListHandles.front();
			AvailableCommandListHandles.pop();

			Handle.Reset(CommandAllocator);
			return Handle;
		}

		return CreateCommandListHandle(CommandAllocator);
	}

	void DiscardCommandList(CommandListHandle& CommandListHandle)
	{
		AvailableCommandListHandles.push(CommandListHandle);
	}

	void ExecuteCommandLists(UINT NumCommandListHandles, CommandListHandle* CommandListHandles, bool WaitForCompletion);

private:
	bool ResolveResourceBarrierCommandList(CommandListHandle& hCmdList, CommandListHandle& hResourceBarrierCmdList);

	CommandListHandle CreateCommandListHandle(CommandAllocator* CommandAllocator)
	{
		CommandListHandle Handle = CommandListHandle(GetParentDevice(), CommandListType, this);
		Handle.Reset(CommandAllocator);
		return Handle;
	}

private:
	const D3D12_COMMAND_LIST_TYPE CommandListType;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> pCommandQueue;
	UINT64									   Frequency = 0;
	Microsoft::WRL::ComPtr<ID3D12Fence>		   Fence;
	CriticalSection							   FenceMutex;
	UINT64									   FenceValue = 1;

	// Command allocators used exclusively for resolving resource barriers
	CommandAllocatorPool ResourceBarrierCommandAllocatorPool;
	CommandAllocator*	 ResourceBarrierCommandAllocator = nullptr;

	std::queue<CommandListHandle> AvailableCommandListHandles;
};
