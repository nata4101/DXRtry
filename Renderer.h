#pragma once
#include "Singleton.h"
#include <dxcapi.h>
#include <vector>

using namespace DirectX;

using Microsoft::WRL::ComPtr;

class Renderer : public Singleton<Renderer>
{
public:
	Renderer(){}
	~Renderer(){}
	void Init();
	void Uninit();
	void Update();
	void Draw();

private:
	static const UINT FrameCount = 2;

	struct Vertex {
		XMFLOAT3 position;
		XMFLOAT4 color;
	};

	// Pipeline objects.
	//パイプラインオブジェクト
	CD3DX12_VIEWPORT m_viewport;
	CD3DX12_RECT m_scissorRect;
	ComPtr<IDXGISwapChain3> m_swapChain;
	ComPtr<ID3D12Device5> m_device;
	ComPtr<ID3D12Resource> m_renderTargets[FrameCount];
	ComPtr<ID3D12CommandAllocator> m_commandAllocator;
	ComPtr<ID3D12CommandQueue> m_commandQueue;
	ComPtr<ID3D12RootSignature> m_rootSignature;
	ComPtr<ID3D12DescriptorHeap> m_rtvHeap;
	ComPtr<ID3D12PipelineState> m_pipelineState;
	ComPtr<ID3D12GraphicsCommandList4> m_commandList;
	UINT m_rtvDescriptorSize;

	// App resources.
	//アプリリソース
	ComPtr<ID3D12Resource> m_vertexBuffer;
	D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

	// Synchronization objects.
	// 同期オブジェクト。
	UINT m_frameIndex;
	HANDLE m_fenceEvent;
	ComPtr<ID3D12Fence> m_fence;
	UINT64 m_fenceValue;

	void LoadPipeline();
	void LoadAssets();
	void PopulateCommandList();
	void WaitForPreviousFrame();

	void CheckRaytracingSupport();

	//virtual void OnKeyUp(UINT8 key);
	bool m_raster = true;
};
