//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************
#include "stdafx.h"

#include "D3D12HelloTriangle.h"

#include "DXRHelper.h"
#include "nv_helpers_dx12/BottomLevelASGenerator.h"

#include "nv_helpers_dx12/RaytracingPipelineGenerator.h"
#include "nv_helpers_dx12/RootSignatureGenerator.h"

D3D12HelloTriangle::D3D12HelloTriangle(UINT width, UINT height,
                                       std::wstring name)
    : DXSample(width, height, name), m_frameIndex(0),
      m_viewport(0.0f, 0.0f, static_cast<float>(width),
                 static_cast<float>(height)),
      m_scissorRect(0, 0, static_cast<LONG>(width), static_cast<LONG>(height)),
      m_rtvDescriptorSize(0) {}

void D3D12HelloTriangle::OnInit() {

  LoadPipeline();
  LoadAssets();

  // Check the raytracing capabilities of the device
  //デバイスのレイトレーシング機能を確認する
  CheckRaytracingSupport();

  // Setup the acceleration structures (AS) for raytracing. When setting up
  // geometry, each bottom-level AS has its own transform matrix.
  //レイトレーシング用の加速構造(AS) を設定します。セットアップ時
  //ジオメトリでは、各最下位 AS に独自の変換行列があります
  CreateAccelerationStructures();

  // Command lists are created in the recording state, but there is
  // nothing to record yet. The main loop expects it to be closed, so
  // close it now.
  //コマンドリストは記録状態で作成しますが、
  //記録するものはまだありません。メイン ループはそれが閉じていることを想定しているため、
  //今すぐ閉じてください。
  ThrowIfFailed(m_commandList->Close());

  // Create the raytracing pipeline, associating the shader code to symbol names
  // and to their root signatures, and defining the amount of memory carried by
  // rays (ray payload)
  //シェーダー コードをシンボル名に関連付けて、レイ トレーシング パイプラインを作成する
  //およびそれらのルート署名、および光線によって運ばれるメモリの量を定義する(光線ペイロード)
  CreateRaytracingPipeline(); // #DXR

  // Allocate the buffer storing the raytracing output, with the same dimensions
  // as the target image
  //レイトレーシング出力を格納するバッファを、ターゲット画像と同じサイズで割り当てます
  CreateRaytracingOutputBuffer(); // #DXR

  // Create the buffer containing the raytracing result (always output in a
  // UAV), and create the heap referencing the resources used by the raytracing,
  // such as the acceleration structure
  //レイトレーシングの結果 (常に UAV で出力) を含むバッファーを作成し、加速構造などのレイトレーシングで使用されるリソースを参照するヒープを作成します。
  CreateShaderResourceHeap(); // #DXR

  // Create the shader binding table and indicating which shaders
  // are invoked for each instance in the  AS
  //シェーダー バインディング テーブルを作成し、AS の各インスタンスに対してどのシェーダーが呼び出されるかを示します。
  CreateShaderBindingTable();
}

// Load the rendering pipeline dependencies.
// レンダリング パイプラインの依存関係を読み込みます。
void D3D12HelloTriangle::LoadPipeline() {
  UINT dxgiFactoryFlags = 0;

#if defined(_DEBUG)
  // Enable the debug layer (requires the Graphics Tools "optional feature").
  // NOTE: Enabling the debug layer after device creation will invalidate the
  // active device.
  //デバッグ レイヤーを有効にします (グラフィック ツールの「オプション機能」が必要です)。
  //注: デバイスの作成後にデバッグ レイヤーを有効にすると、アクティブなデバイス。
  {
    ComPtr<ID3D12Debug> debugController;
    if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&debugController)))) {
      debugController->EnableDebugLayer();

      // Enable additional debug layers.
      dxgiFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
    }
  }
#endif

  ComPtr<IDXGIFactory4> factory;
  ThrowIfFailed(CreateDXGIFactory2(dxgiFactoryFlags, IID_PPV_ARGS(&factory)));

  if (m_useWarpDevice) {
    ComPtr<IDXGIAdapter> warpAdapter;
    ThrowIfFailed(factory->EnumWarpAdapter(IID_PPV_ARGS(&warpAdapter)));

    ThrowIfFailed(D3D12CreateDevice(warpAdapter.Get(), D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&m_device)));
  } else {
    ComPtr<IDXGIAdapter1> hardwareAdapter;
    GetHardwareAdapter(factory.Get(), &hardwareAdapter);

    ThrowIfFailed(D3D12CreateDevice(hardwareAdapter.Get(),
                                    D3D_FEATURE_LEVEL_11_0,
                                    IID_PPV_ARGS(&m_device)));
  }

  // c
  D3D12_COMMAND_QUEUE_DESC queueDesc = {};
  queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
  queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;

  ThrowIfFailed(
      m_device->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&m_commandQueue)));

  // Describe and create the swap chain.
  // スワップ チェーンを記述して作成します。
  DXGI_SWAP_CHAIN_DESC1 swapChainDesc = {};
  swapChainDesc.BufferCount = FrameCount;
  swapChainDesc.Width = m_width;
  swapChainDesc.Height = m_height;
  swapChainDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
  swapChainDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
  swapChainDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
  swapChainDesc.SampleDesc.Count = 1;

  ComPtr<IDXGISwapChain1> swapChain;
  ThrowIfFailed(factory->CreateSwapChainForHwnd(
      m_commandQueue.Get(), // Swap chain needs the queue so that it can force aflush on it.
						    // スワップ チェーンには、強制的にフラッシュできるようにキューが必要です。
      Win32Application::GetHwnd(), &swapChainDesc, nullptr, nullptr,
      &swapChain));

  // This sample does not support fullscreen transitions.
  // このサンプルは全画面遷移をサポートしていません。
  ThrowIfFailed(factory->MakeWindowAssociation(Win32Application::GetHwnd(),
                                               DXGI_MWA_NO_ALT_ENTER));

  ThrowIfFailed(swapChain.As(&m_swapChain));
  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();

  // Create descriptor heaps.
  // 記述子ヒープを作成します。
  {
    // Describe and create a render target view (RTV) descriptor heap.
	// レンダー ターゲット ビュー (RTV) 記述子ヒープを記述して作成します。
    D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc = {};
    rtvHeapDesc.NumDescriptors = FrameCount;
    rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
    rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
    ThrowIfFailed(
        m_device->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(&m_rtvHeap)));

    m_rtvDescriptorSize = m_device->GetDescriptorHandleIncrementSize(
        D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
  }

  // Create frame resources.
  // フレーム リソースを作成します。
  {
    CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
        m_rtvHeap->GetCPUDescriptorHandleForHeapStart());

    // Create a RTV for each frame.
	// 各フレームの RTV を作成します。
    for (UINT n = 0; n < FrameCount; n++) {
      ThrowIfFailed(
          m_swapChain->GetBuffer(n, IID_PPV_ARGS(&m_renderTargets[n])));
      m_device->CreateRenderTargetView(m_renderTargets[n].Get(), nullptr,
                                       rtvHandle);
      rtvHandle.Offset(1, m_rtvDescriptorSize);
    }
  }

  ThrowIfFailed(m_device->CreateCommandAllocator(
      D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&m_commandAllocator)));
}

// Load the sample assets.
// サンプル アセットを読み込みます。
void D3D12HelloTriangle::LoadAssets() {
  // Create an empty root signature.
  // 空のルート署名を作成します。
  {
    CD3DX12_ROOT_SIGNATURE_DESC rootSignatureDesc;
    rootSignatureDesc.Init(
        0, nullptr, 0, nullptr,
        D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

    ComPtr<ID3DBlob> signature;
    ComPtr<ID3DBlob> error;
    ThrowIfFailed(D3D12SerializeRootSignature(
        &rootSignatureDesc, D3D_ROOT_SIGNATURE_VERSION_1, &signature, &error));
    ThrowIfFailed(m_device->CreateRootSignature(
        0, signature->GetBufferPointer(), signature->GetBufferSize(),
        IID_PPV_ARGS(&m_rootSignature)));
  }

  // Create the pipeline state, which includes compiling and loading shaders.
  // シェーダーのコンパイルと読み込みを含むパイプライン状態を作成します。
  {
    ComPtr<ID3DBlob> vertexShader;
    ComPtr<ID3DBlob> pixelShader;

#if defined(_DEBUG)
    // Enable better shader debugging with the graphics debugging tools.
    // グラフィック デバッグ ツールを使用して、より適切なシェーダー デバッグを有効にします。
    UINT compileFlags = D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION;
#else
    UINT compileFlags = 0;
#endif

    ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(),
                                     nullptr, nullptr, "VSMain", "vs_5_0",
                                     compileFlags, 0, &vertexShader, nullptr));
    ThrowIfFailed(D3DCompileFromFile(GetAssetFullPath(L"shaders.hlsl").c_str(),
                                     nullptr, nullptr, "PSMain", "ps_5_0",
                                     compileFlags, 0, &pixelShader, nullptr));

    // Define the vertex input layout.
    // 頂点入力レイアウトを定義します。
    D3D12_INPUT_ELEMENT_DESC inputElementDescs[] = {
        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0},
        {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
         D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0}};

    // Describe and create the graphics pipeline state object (PSO).
    // グラフィックス パイプライン状態オブジェクト (PSO) を記述して作成します。
    D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = {};
    psoDesc.InputLayout = {inputElementDescs, _countof(inputElementDescs)};
    psoDesc.pRootSignature = m_rootSignature.Get();
    psoDesc.VS = CD3DX12_SHADER_BYTECODE(vertexShader.Get());
    psoDesc.PS = CD3DX12_SHADER_BYTECODE(pixelShader.Get());
    psoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
    psoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
    psoDesc.DepthStencilState.DepthEnable = FALSE;
    psoDesc.DepthStencilState.StencilEnable = FALSE;
    psoDesc.SampleMask = UINT_MAX;
    psoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
    psoDesc.NumRenderTargets = 1;
    psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
    psoDesc.SampleDesc.Count = 1;
    ThrowIfFailed(m_device->CreateGraphicsPipelineState(
        &psoDesc, IID_PPV_ARGS(&m_pipelineState)));
  }

  // Create the command list.
  // コマンドリストを作成します。
  ThrowIfFailed(m_device->CreateCommandList(
      0, D3D12_COMMAND_LIST_TYPE_DIRECT, m_commandAllocator.Get(),
      m_pipelineState.Get(), IID_PPV_ARGS(&m_commandList)));

  // Create the vertex buffer.
  // 頂点バッファを作成します。
  {
    // Define the geometry for a triangle.
	// 三角形のジオメトリを定義します。
    Vertex triangleVertices[] = {
        {{0.0f, 0.25f * m_aspectRatio, 0.0f}, {1.0f, 1.0f, 0.0f, 1.0f}},
        {{0.25f, -0.25f * m_aspectRatio, 0.0f}, {0.0f, 1.0f, 1.0f, 1.0f}},
        {{-0.25f, -0.25f * m_aspectRatio, 0.0f}, {1.0f, 0.0f, 1.0f, 1.0f}}};

    const UINT vertexBufferSize = sizeof(triangleVertices);

    // Note: using upload heaps to transfer static data like vert buffers is not
    // recommended. Every time the GPU needs it, the upload heap will be
    // marshalled over. Please read up on Default Heap usage. An upload heap is
    // used here for code simplicity and because there are very few verts to
    // actually transfer.
	// 注: アップロード ヒープを使用して、vert バッファーのような静的データを転送することはできません。
	//推奨。 GPU が必要とするたびに、アップロード ヒープが整列化されます。デフォルトのヒープの使用法をよく読んでください。
	//アップロード ヒープは、コードを簡素化するために、また実際に転送する頂点がほとんどないため、ここで使用されます。
    ThrowIfFailed(m_device->CreateCommittedResource(
        &CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD), D3D12_HEAP_FLAG_NONE,
        &CD3DX12_RESOURCE_DESC::Buffer(vertexBufferSize),
        D3D12_RESOURCE_STATE_GENERIC_READ, nullptr,
        IID_PPV_ARGS(&m_vertexBuffer)));

    // Copy the triangle data to the vertex buffer.
	// 三角形データを頂点バッファーにコピーします。
    UINT8 *pVertexDataBegin;
    CD3DX12_RANGE readRange(0, 0); // We do not intend to read from this resource on the CPU.
								   // CPU 上のこのリソースから読み取るつもりはありません。
    ThrowIfFailed(m_vertexBuffer->Map(
        0, &readRange, reinterpret_cast<void **>(&pVertexDataBegin)));
    memcpy(pVertexDataBegin, triangleVertices, sizeof(triangleVertices));
    m_vertexBuffer->Unmap(0, nullptr);

    // Initialize the vertex buffer view.
	// 頂点バッファー ビューを初期化します。
    m_vertexBufferView.BufferLocation = m_vertexBuffer->GetGPUVirtualAddress();
    m_vertexBufferView.StrideInBytes = sizeof(Vertex);
    m_vertexBufferView.SizeInBytes = vertexBufferSize;
  }

  // Create synchronization objects and wait until assets have been uploaded to the GPU.
  // 同期オブジェクトを作成し、アセットが GPU にアップロードされるまで待ちます。
  {
    ThrowIfFailed(m_device->CreateFence(0, D3D12_FENCE_FLAG_NONE,
                                        IID_PPV_ARGS(&m_fence)));
    m_fenceValue = 1;

    // Create an event handle to use for frame synchronization.
	// フレーム同期に使用するイベント ハンドルを作成します。
    m_fenceEvent = CreateEvent(nullptr, FALSE, FALSE, nullptr);
    if (m_fenceEvent == nullptr) {
      ThrowIfFailed(HRESULT_FROM_WIN32(GetLastError()));
    }

    // Wait for the command list to execute; 
	//we are reusing the same commandlist in our main loop but for now,
	//we just want to wait for setup tocomplete before continuing.
	// コマンド リストが実行されるのを待ちます。
	//メインループで同じコマンドリストを再利用していますが、今はセットアップが完了するのを待ってから続行します。
    WaitForPreviousFrame();
  }
}

// Update frame-based values.
// フレームベースの値を更新します。
void D3D12HelloTriangle::OnUpdate() {}

// Render the scene.
// シーンをレンダリングします。
void D3D12HelloTriangle::OnRender() {
  // Record all the commands we need to render the scene into the command list.
  // シーンをレンダリングするために必要なすべてのコマンドをコマンド リストに記録します。
	PopulateCommandList();

  // Execute the command list.
  // コマンドリストを実行します。
  ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
  m_commandQueue->ExecuteCommandLists(_countof(ppCommandLists), ppCommandLists);

  // Present the frame.
  // フレームを表示します。
  ThrowIfFailed(m_swapChain->Present(1, 0));

  WaitForPreviousFrame();
}

void D3D12HelloTriangle::OnDestroy() {
  // Ensure that the GPU is no longer referencing resources that are about to be
  // cleaned up by the destructor.
  // デストラクタによってクリーンアップされようとしているリソースを GPU が参照していないことを確認してください。
  WaitForPreviousFrame();

  CloseHandle(m_fenceEvent);
}

void D3D12HelloTriangle::PopulateCommandList() {
  // Command list allocators can only be reset when the associated
  // command lists have finished execution on the GPU; apps should use
  // fences to determine GPU execution progress.

  //コマンドリストアロケータは、関連するコマンド リストが GPU での実行を終了した場合にのみリセットできます。
  //アプリはフェンスを使用して GPU 実行の進行状況を判断する必要があります。
  ThrowIfFailed(m_commandAllocator->Reset());

  // However, when ExecuteCommandList() is called on a particular commandlist, 
  // that command list can then be reset at any time and must be before re-recording.
  // ただし、特定のコマンドリストでExecuteCommandList()が呼び出された場合、
  // そのコマンドリストはいつでもリセットでき、再記録する前に設定する必要があります。
  ThrowIfFailed(
      m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

  // Set necessary state.
  // 必要な状態を設定します。
  m_commandList->SetGraphicsRootSignature(m_rootSignature.Get());
  m_commandList->RSSetViewports(1, &m_viewport);
  m_commandList->RSSetScissorRects(1, &m_scissorRect);

  // Indicate that the back buffer will be used as a render target.
  // バック バッファーがレンダー ターゲットとして使用されることを示します。
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                        m_renderTargets[m_frameIndex].Get(),
                                        D3D12_RESOURCE_STATE_PRESENT,
                                        D3D12_RESOURCE_STATE_RENDER_TARGET));

  CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHandle(
      m_rtvHeap->GetCPUDescriptorHandleForHeapStart(), m_frameIndex,
      m_rtvDescriptorSize);
  m_commandList->OMSetRenderTargets(1, &rtvHandle, FALSE, nullptr);

  // Record commands.
  // コマンドを記録します。
  // #DXR
  if (m_raster) {
    const float clearColor[] = {0.0f, 0.2f, 0.4f, 1.0f};
    m_commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    m_commandList->ClearRenderTargetView(rtvHandle, clearColor, 0, nullptr);
    m_commandList->IASetVertexBuffers(0, 1, &m_vertexBufferView);
    m_commandList->DrawInstanced(3, 1, 0, 0);
  } else {
    // #DXR
    // Bind the descriptor heap giving access to the top-level acceleration structure, 
	// as well as the raytracing output
	// 最上位のアクセラレーション構造とレイトレーシング出力へのアクセスを提供する記述子ヒープをバインドします
    std::vector<ID3D12DescriptorHeap *> heaps = {m_srvUavHeap.Get()};
    m_commandList->SetDescriptorHeaps(static_cast<UINT>(heaps.size()),
                                      heaps.data());

    // On the last frame, the raytracing output was used as a copy source, to
    // copy its contents into the render target. Now we need to transition it to
    // a UAV so that the shaders can write in it.
	// 最後のフレームで、レイトレーシング出力がコピー ソースとして使用され、そのコンテンツがレンダー ターゲットにコピーされました。
	//次に、シェーダーが書き込むことができるように、UAV に移行する必要があります。
    CD3DX12_RESOURCE_BARRIER transition = CD3DX12_RESOURCE_BARRIER::Transition(
        m_outputResource.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE,
        D3D12_RESOURCE_STATE_UNORDERED_ACCESS);
    m_commandList->ResourceBarrier(1, &transition);

    // Setup the raytracing task
	// レイトレーシング タスクをセットアップします
    D3D12_DISPATCH_RAYS_DESC desc = {};
    // The layout of the SBT is as follows: ray generation shader, 
    // miss shaders, hit groups. As described in the CreateShaderBindingTable method,
    // all SBT entries of a given type have the same size to allow a fixed stride.
    // SBT のレイアウトは次のとおりです: 
	//レイ生成シェーダー、ミスシェーダー、ヒット グループ。 CreateShaderBindingTable メソッドで説明されているように、特定のタイプのすべての SBT エントリは、固定ストライドを可能にするために同じサイズになります。

    // The ray generation shaders are always at the beginning of the SBT.
	// レイ生成シェーダーは常に SBT の先頭にあります。
    uint32_t rayGenerationSectionSizeInBytes =
        m_sbtHelper.GetRayGenSectionSize();
    desc.RayGenerationShaderRecord.StartAddress =
        m_sbtStorage->GetGPUVirtualAddress();
    desc.RayGenerationShaderRecord.SizeInBytes =
        rayGenerationSectionSizeInBytes;

    // The miss shaders are in the second SBT section, right after the raygeneration shader. 
	//We have one miss shader for the camera rays and onefor the shadow rays, 
	//so this section has a size of 2*m_sbtEntrySize. 
    //We also indicate the stride between the two miss shaders, which is the sizeof a SBT entry
    // ミスシェーダーは、レイ生成シェーダーの直後の 2 番目の SBT セクションにあります。
	//カメラレイに1つのミスシェーダーがあり、シャドウレイに1つあるため、このセクションのサイズは 2*m_sbtEntrySize です。
	//また、SBT エントリのサイズである 2 つのミス シェーダー間のストライドも示します。
    uint32_t missSectionSizeInBytes = m_sbtHelper.GetMissSectionSize();
    desc.MissShaderTable.StartAddress =
        m_sbtStorage->GetGPUVirtualAddress() + rayGenerationSectionSizeInBytes;
    desc.MissShaderTable.SizeInBytes = missSectionSizeInBytes;
    desc.MissShaderTable.StrideInBytes = m_sbtHelper.GetMissEntrySize();

    // The hit groups section start after the miss shaders. 
    // In this sample we have one 1 hit group for the triangle
	// ヒット グループ セクションは、ミス シェーダーの後に始まります。
	//このサンプルでは、​​三角形に 1 つのヒット グループがあります。
    uint32_t hitGroupsSectionSize = m_sbtHelper.GetHitGroupSectionSize();
    desc.HitGroupTable.StartAddress = m_sbtStorage->GetGPUVirtualAddress() +
                                      rayGenerationSectionSizeInBytes +
                                      missSectionSizeInBytes;
    desc.HitGroupTable.SizeInBytes = hitGroupsSectionSize;
    desc.HitGroupTable.StrideInBytes = m_sbtHelper.GetHitGroupEntrySize();

    // Dimensions of the image to render, identical to a kernel launch dimension
	// レンダリングする画像の寸法、カーネル起動寸法と同じ
    desc.Width = GetWidth();
    desc.Height = GetHeight();
    desc.Depth = 1;

    // Bind the raytracing pipeline
	// レイトレーシング パイプラインをバインドする
    m_commandList->SetPipelineState1(m_rtStateObject.Get());
    // Dispatch the rays and write to the raytracing output
	// レイをディスパッチし、レイトレーシング出力に書き込みます
    m_commandList->DispatchRays(&desc);

    // The raytracing output needs to be copied to the actual render target usedfor display. 
    // For this, we need to transition the raytracing output from aUAV to a copy source, and the render target buffer to a copy destination.
    // We can then do the actual copy, before transitioning the render target buffer into a render target, that will be then used to display the image
    //レイトレーシング出力は、表示に使用される実際のレンダーターゲットにコピーする必要があります。
	//このためには、レイトレーシング出力を UAV からコピー ソースに移行し、レンダー ターゲット バッファーをコピー先に移行する必要があります。
	//次に、レンダー ターゲット バッファーをレンダー ターゲットに移行する前に、実際のコピーを実行できます。これは、イメージの表示に使用されます。
    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        m_outputResource.Get(), D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
        D3D12_RESOURCE_STATE_COPY_SOURCE);
    m_commandList->ResourceBarrier(1, &transition);
    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_RENDER_TARGET,
        D3D12_RESOURCE_STATE_COPY_DEST);
    m_commandList->ResourceBarrier(1, &transition);

    m_commandList->CopyResource(m_renderTargets[m_frameIndex].Get(),
                                m_outputResource.Get());

    transition = CD3DX12_RESOURCE_BARRIER::Transition(
        m_renderTargets[m_frameIndex].Get(), D3D12_RESOURCE_STATE_COPY_DEST,
        D3D12_RESOURCE_STATE_RENDER_TARGET);
    m_commandList->ResourceBarrier(1, &transition);
  }

  // Indicate that the back buffer will now be used to present.
  // バック バッファーが表示に使用されることを示します。
  m_commandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(
                                        m_renderTargets[m_frameIndex].Get(),
                                        D3D12_RESOURCE_STATE_RENDER_TARGET,
                                        D3D12_RESOURCE_STATE_PRESENT));

  ThrowIfFailed(m_commandList->Close());
}

void D3D12HelloTriangle::WaitForPreviousFrame() {
  // WAITING FOR THE FRAME TO COMPLETE BEFORE CONTINUING IS NOT BEST PRACTICE.
  // This is code implemented as such for simplicity. 
  // The D3D12HelloFrameBuffering sample illustrates how to use fences for efficient
  // resource usage and to maximize GPU utilization.
  // 続行する前にフレームが完了するのを待つことは最善の方法ではありません。これは、簡単にするために実装されたコードです。
  // D3D12HelloFrameBuffering サンプルは、フェンスを使用してリソースを効率的に使用し、GPU の使用率を最大化する方法を示しています。

  // Signal and increment the fence value.
  // フェンスの値を通知してインクリメントします。
  const UINT64 fence = m_fenceValue;
  ThrowIfFailed(m_commandQueue->Signal(m_fence.Get(), fence));
  m_fenceValue++;

  // Wait until the previous frame is finished.
  // 前のフレームが終了するまで待ちます。
  if (m_fence->GetCompletedValue() < fence) {
    ThrowIfFailed(m_fence->SetEventOnCompletion(fence, m_fenceEvent));
    WaitForSingleObject(m_fenceEvent, INFINITE);
  }

  m_frameIndex = m_swapChain->GetCurrentBackBufferIndex();
}

void D3D12HelloTriangle::CheckRaytracingSupport() {
  D3D12_FEATURE_DATA_D3D12_OPTIONS5 options5 = {};
  ThrowIfFailed(m_device->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,
                                              &options5, sizeof(options5)));
  if (options5.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
    throw std::runtime_error("Raytracing not supported on device");
}

//-----------------------------------------------------------------------------
//
//
void D3D12HelloTriangle::OnKeyUp(UINT8 key) {
  // Alternate between rasterization and raytracing using the spacebar
  // スペースバーを使用してラスタライズとレイトレーシングを交互に行う
  if (key == VK_SPACE) {
    m_raster = !m_raster;
  }
}

//-----------------------------------------------------------------------------
//
// Create a bottom-level acceleration structure based on a list of vertex
// buffers in GPU memory along with their vertex count. The build is then done
// in 3 steps: gathering the geometry, computing the sizes of the required
// buffers, and building the actual AS
//GPU メモリ内の頂点バッファーのリストとその頂点数に基づいて、最下位レベルのアクセラレーション構造を作成します。
//これでビルドは完了です
//3 つのステップ: ジオメトリの収集、必要なバッファーのサイズの計算、実際の AS の構築

D3D12HelloTriangle::AccelerationStructureBuffers
D3D12HelloTriangle::CreateBottomLevelAS(
    std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers) {
  nv_helpers_dx12::BottomLevelASGenerator bottomLevelAS;

  // Adding all vertex buffers and not transforming their position.
  // すべての頂点バッファーを追加し、それらの位置を変換しません。
  for (const auto &buffer : vVertexBuffers) {
    bottomLevelAS.AddVertexBuffer(buffer.first.Get(), 0, buffer.second,
                                  sizeof(Vertex), 0, 0);
  }

  // The AS build requires some scratch space to store temporary information.
  // AS ビルドには、一時的な情報を保存するためのスクラッチ スペースが必要です。
  // The amount of scratch memory is dependent on the scene complexity.
  // スクラッチ メモリの量は、シーンの複雑さに依存します。
  UINT64 scratchSizeInBytes = 0;
  // The final AS also needs to be stored in addition to the existing vertex buffers. 
  // 既存の頂点バッファーに加えて、最終的な AS も保存する必要があります。
  // It size is also dependent on the scene complexity.
  // サイズもシーンの複雑さに依存します。
  UINT64 resultSizeInBytes = 0;

  bottomLevelAS.ComputeASBufferSizes(m_device.Get(), false, &scratchSizeInBytes,
                                     &resultSizeInBytes);

  // Once the sizes are obtained, the application is responsible for allocatingthe necessary buffers. 
  // サイズが取得されると、アプリケーションは必要なバッファを割り当てる責任があります
  // Since the entire generation will be done on the GPU,
  // we can directly allocate those on the default heap
  // 生成全体が GPU で行われるため、デフォルトのヒープに直接割り当てることができます
  AccelerationStructureBuffers buffers;
  buffers.pScratch = nv_helpers_dx12::CreateBuffer(
      m_device.Get(), scratchSizeInBytes,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON,
      nv_helpers_dx12::kDefaultHeapProps);
  buffers.pResult = nv_helpers_dx12::CreateBuffer(
      m_device.Get(), resultSizeInBytes,
      D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
      nv_helpers_dx12::kDefaultHeapProps);

  // Build the acceleration structure. Note that this call integrates a barrier on the generated AS,
  // so that it can be used to compute a top-level AS right after this method.
  // 加速構造を構築します。この呼び出しは、生成された AS にバリアを統合することに注意してください。
  // このメソッドの直後にトップレベル AS を計算するために使用できるようにします。
  bottomLevelAS.Generate(m_commandList.Get(), buffers.pScratch.Get(),
                         buffers.pResult.Get(), false, nullptr);

  return buffers;
}

//-----------------------------------------------------------------------------
// Create the main acceleration structure that holds all instances of the scene.
// Similarly to the bottom-level AS generation, it is done in 3 steps: gathering
// the instances, computing the memory requirements for the AS, and building the AS itself
// シーンのすべてのインスタンスを保持するメインの加速構造を作成します。
// 最下位レベルの AS 生成と同様に、インスタンスの収集、AS のメモリ要件の計算、AS 自体の構築という 3 つのステップで実行されます。
void D3D12HelloTriangle::CreateTopLevelAS(
    const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>
        &instances // pair of bottom level AS and matrix of the instance
				   // インスタンスの最下位 AS と行列のペア
) {
  // Gather all the instances into the builder helper
  // すべてのインスタンスをビルダー ヘルパーに集める
  for (size_t i = 0; i < instances.size(); i++) {
    m_topLevelASGenerator.AddInstance(instances[i].first.Get(),
                                      instances[i].second, static_cast<UINT>(i),
                                      static_cast<UINT>(0));
  }

  // As for the bottom-level AS, the building the AS requires some scratch space
  // to store temporary data in addition to the actual AS. In the case of the
  // top-level AS, the instance descriptors also need to be stored in GPU
  // memory. This call outputs the memory requirements for each (scratch,
  // results, instance descriptors) so that the application can allocate the
  // corresponding memory

  // 最下位の AS に関して言えば、AS の構築には、実際の AS に加えて一時データを保存するためのスクラッチ スペースが必要です。
  // トップレベル AS の場合、インスタンス記述子も GPU メモリに保存する必要があります。
  // この呼び出しは、アプリケーションが対応するメモリを割り当てることができるように、それぞれのメモリ要件 (スクラッチ、結果、インスタンス記述子) を出力します。
  UINT64 scratchSize, resultSize, instanceDescsSize;

  m_topLevelASGenerator.ComputeASBufferSizes(m_device.Get(), true, &scratchSize,
                                             &resultSize, &instanceDescsSize);

  // Create the scratch and result buffers. 
  // スクラッチと結果のバッファを作成します。
  // Since the build is all done on GPU,those can be allocated on the default heap
  // ビルドはすべて GPU で行われるため、デフォルトのヒープに割り当てることができます
  m_topLevelASBuffers.pScratch = nv_helpers_dx12::CreateBuffer(
      m_device.Get(), scratchSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
      nv_helpers_dx12::kDefaultHeapProps);
  m_topLevelASBuffers.pResult = nv_helpers_dx12::CreateBuffer(
      m_device.Get(), resultSize, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS,
      D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE,
      nv_helpers_dx12::kDefaultHeapProps);

  // The buffer describing the instances: ID, shader binding information,
  // matrices ... Those will be copied into the buffer by the helper through
  // mapping, so the buffer has to be allocated on the upload heap.
  //インスタンスを記述するバッファー: ID、シェーダー バインディング情報、行列 ... 
  //これらは、マッピングを通じてヘルパーによってバッファーにコピーされるため、バッファーはアップロード ヒープに割り当てる必要があります。
  m_topLevelASBuffers.pInstanceDesc = nv_helpers_dx12::CreateBuffer(
      m_device.Get(), instanceDescsSize, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);

  // After all the buffers are allocated, or if only an update is required, we
  // can build the acceleration structure. Note that in the case of the update
  // we also pass the existing AS as the 'previous' AS, so that it can be refitted in place.
  //すべてのバッファが割り当てられた後、または更新のみが必要な場合は、加速構造を構築できます。
  //更新の場合、既存の AS を「以前の」AS として渡すことで、所定の位置に再装着できることに注意してください。
  m_topLevelASGenerator.Generate(m_commandList.Get(),
                                 m_topLevelASBuffers.pScratch.Get(),
                                 m_topLevelASBuffers.pResult.Get(),
                                 m_topLevelASBuffers.pInstanceDesc.Get());
}

//-----------------------------------------------------------------------------
//
// Combine the BLAS and TLAS builds to construct the entire acceleration
// structure required to raytrace the scene
// BLAS ビルドと TLAS ビルドを組み合わせて、シーンのレイトレースに必要な加速構造全体を構築します
void D3D12HelloTriangle::CreateAccelerationStructures() {
  // Build the bottom AS from the Triangle vertex buffer
  // Triangle 頂点バッファーから下の AS を構築します
  AccelerationStructureBuffers bottomLevelBuffers =
      CreateBottomLevelAS({{m_vertexBuffer.Get(), 3}});

  // Just one instance for now
  // 現時点では 1 つのインスタンスのみ
  m_instances = {{bottomLevelBuffers.pResult, XMMatrixIdentity()}};
  CreateTopLevelAS(m_instances);

  // Flush the command list and wait for it to finish
  // コマンドリストをフラッシュし、完了するのを待ちます
  m_commandList->Close();
  ID3D12CommandList *ppCommandLists[] = {m_commandList.Get()};
  m_commandQueue->ExecuteCommandLists(1, ppCommandLists);
  m_fenceValue++;
  m_commandQueue->Signal(m_fence.Get(), m_fenceValue);

  m_fence->SetEventOnCompletion(m_fenceValue, m_fenceEvent);
  WaitForSingleObject(m_fenceEvent, INFINITE);

  // Once the command list is finished executing, reset it to be reused for rendering
  // コマンド リストの実行が終了したら、レンダリングに再利用するためにリセットします
  ThrowIfFailed(
      m_commandList->Reset(m_commandAllocator.Get(), m_pipelineState.Get()));

  // Store the AS buffers. The rest of the buffers will be released once we exit the function
  // AS バッファを保存します。関数を終了すると、残りのバッファは解放されます。
  m_bottomLevelAS = bottomLevelBuffers.pResult;
}

//-----------------------------------------------------------------------------
// The ray generation shader needs to access 2 resources: the raytracing output
// and the top-level acceleration structure
// レイ生成シェーダーは2つのリソースにアクセスする必要があります: レイトレーシング出力とトップレベルのアクセラレーション構造


ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateRayGenSignature() {
  nv_helpers_dx12::RootSignatureGenerator rsc;
  rsc.AddHeapRangesParameter(
      {{0 /*u0*/, 1 /*1 descriptor(記述子) */, 0 /*use the implicit register space(暗黙のレジスタ空間を使用する) 0*/,
        D3D12_DESCRIPTOR_RANGE_TYPE_UAV /* UAV representing the output buffer(出力バッファを表す UAV)*/,
        0 /*heap slot where the UAV is defined(UAV が定義されているヒープ スロット)*/},
       {0 /*t0*/, 1, 0,
        D3D12_DESCRIPTOR_RANGE_TYPE_SRV /*Top-level acceleration structure(トップレベルの加速構造)*/,
        1}
	  });

  return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// The hit shader communicates only through the ray payload, and therefore does not require any resources
// ヒットシェーダーはレイペイロードのみを介して通信するため,リソースは必要ありません
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateHitSignature() {
  nv_helpers_dx12::RootSignatureGenerator rsc;
  rsc.AddRootParameter(D3D12_ROOT_PARAMETER_TYPE_SRV);
  return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
// The miss shader communicates only through the ray payload, and therefore does not require any resources
// missシェーダーはレイペイロードのみを介して通信するため、リソースは必要ありません
ComPtr<ID3D12RootSignature> D3D12HelloTriangle::CreateMissSignature() {
  nv_helpers_dx12::RootSignatureGenerator rsc;
  return rsc.Generate(m_device.Get(), true);
}

//-----------------------------------------------------------------------------
//
// The raytracing pipeline binds the shader code, root signatures and pipeline
// characteristics in a single structure used by DXR to invoke the shaders and
// manage temporary memory during raytracing
// レイトレーシング パイプラインは、シェーダーコード、ルート署名、およびパイプライン特性を DXR が使用する単一の構造にバインドし、
// レイトレーシング中にシェーダーを呼び出し、一時メモリを管理します。
//
//
void D3D12HelloTriangle::CreateRaytracingPipeline() {
  nv_helpers_dx12::RayTracingPipelineGenerator pipeline(m_device.Get());

  // The pipeline contains the DXIL code of all the shaders potentially executed
  // during the raytracing process. This section compiles the HLSL code into a
  // set of DXIL libraries. We chose to separate the code in several libraries
  // by semantic (ray generation, hit, miss) for clarity. Any code layout can be used.
  // パイプラインには、レイトレーシングプロセス中に実行される可能性のあるすべてのシェーダーの DXIL コードが含まれています。
  // このセクションでは、HLSL コードを DXIL ライブラリのセットにコンパイルします。
  // 明確にするために、いくつかのライブラリのコードをセマンティック (レイ生成、ヒット、ミス) ごとに分けることにしました。
  // 任意のコード レイアウトを使用できます。
  m_rayGenLibrary = nv_helpers_dx12::CompileShaderLibrary(L"RayGen.hlsl");
  m_missLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Miss.hlsl");
  m_hitLibrary = nv_helpers_dx12::CompileShaderLibrary(L"Hit.hlsl");

  // In a way similar to DLLs, each library is associated with a number of exported symbols. 
  // DLL と同様に、各ライブラリは、エクスポートされた多数のシンボルに関連付けられています。
  // This has to be done explicitly in the lines below. 
  // これは、以下の行で明示的に行う必要があります。
  // Note that a single library can contain an arbitrary number of symbols, 
  // whose semantic is given in HLSL using the [shader("xxx")] syntax
  // 単一のライブラリには任意の数のシンボルを含めることができ、
  // そのセマンティクスは [shader("xxx")] 構文を使用して HLSL で指定されることに注意してください。
  pipeline.AddLibrary(m_rayGenLibrary.Get(), {L"RayGen"});
  pipeline.AddLibrary(m_missLibrary.Get(), {L"Miss"});
  pipeline.AddLibrary(m_hitLibrary.Get(), {L"ClosestHit"});

  // To be used, each DX12 shader needs a root signature defining which parameters and buffers will be accessed.
  // 使用するには,各DX12 シェーダーに、アクセスするパラメーターとバッファーを定義するルート署名が必要です
  m_rayGenSignature = CreateRayGenSignature();
  m_missSignature = CreateMissSignature();
  m_hitSignature = CreateHitSignature();

  // 3 different shaders can be invoked to obtain an intersection:
  // an intersection shader is called
  // when hitting the bounding box of non-triangular geometry. This is beyond
  // the scope of this tutorial. An any-hit shader is called on potential
  // intersections. This shader can, for example, perform alpha-testing and
  // discard some intersections. Finally, the closest-hit program is invoked on
  // the intersection point closest to the ray origin. Those 3 shaders are bound
  // together into a hit group.
  // 3つの異なるシェーダーを呼び出して交差を取得できます。
  // 交差シェーダーは、非三角形ジオメトリの境界ボックスにぶつかったときに呼び出されます。
  // これは、このチュートリアルの範囲を超えています。エニーヒット シェーダーは、潜在的な交差点で呼び出されます。
  // このシェーダーは、たとえば、アルファテストを実行して、一部の交差を破棄できます。
  // 最後に、レイの原点に最も近い交差点で、最も近いヒットプログラムが呼び出されます。
  // これらの 3つのシェーダーは、1つのヒット グループにまとめられます。

  // Note that for triangular geometry the intersection shader is built-in. 
  // An empty any-hit shader is also defined by default, so in our simple case each
  // hit group contains only the closest hit shader. Note that since the
  // exported symbols are defined above the shaders can be simply referred to by name.
  // 三角形のジオメトリの場合、交差シェーダーが組み込まれていることに注意してください。
  // 空のエニーヒット シェーダーもデフォルトで定義されているため、単純なケースでは、各ヒット グループには最も近いヒットシェーダーのみが含まれます。
  //エクスポートされたシンボルは上記で定義されているため、シェーダーは単に名前で参照できることに注意してください。

  // Hit group for the triangles, with a shader simply interpolating vertex colors
  // 頂点の色を単純に補間するシェーダーを使用して、三角形のグループをヒット
  pipeline.AddHitGroup(L"HitGroup", L"ClosestHit");

  // The following section associates the root signature to each shader. Note
  // that we can explicitly show that some shaders share the same root signature
  // (eg. Miss and ShadowMiss). Note that the hit shaders are now only referred
  // to as hit groups, meaning that the underlying intersection, any-hit and
  // closest-hit shaders share the same root signature.
  // 次のセクションでは、ルート署名を各シェーダーに関連付けます。
  // 一部のシェーダーが同じルート署名 (たとえば、Miss と ShadowMiss) を共有していることを明示的に示すことができることに注意してください。
  // ヒット シェーダーは現在、ヒット グループとのみ呼ばれていることに注意してください。
  // つまり、基になる交差、任意のヒット、および最も近いヒットのシェーダーが同じルート シグネチャを共有することを意味します。
  pipeline.AddRootSignatureAssociation(m_rayGenSignature.Get(), {L"RayGen"});
  pipeline.AddRootSignatureAssociation(m_missSignature.Get(), {L"Miss"});
  pipeline.AddRootSignatureAssociation(m_hitSignature.Get(), {L"HitGroup"});

  // The payload size defines the maximum size of the data carried by the rays, ie.
  // the the data exchanged between shaders, such as the HitInfo structure in the HLSL code.
  // It is important to keep this value as low as possible as a too high value
  // would result in unnecessary memory consumption and cache trashing.
  // ペイロード サイズは、光線によって運ばれるデータの最大サイズを定義します。 
  // HLSL コードの HitInfo 構造など、シェーダー間で交換されるデータ。この値をできるだけ低く保つことが重要です。
  //値が高すぎると、不要なメモリ消費とキャッシュの廃棄が発生するためです。
  pipeline.SetMaxPayloadSize(4 * sizeof(float)); // RGB + distance

  // Upon hitting a surface, DXR can provide several attributes to the hit. 
  // In our sample we just use the barycentric coordinates defined by the weights
  // u,v of the last two vertices of the triangle. The actual barycentrics can
  // be obtained using float3 barycentrics = float3(1.f-u-v, u, v);
  // 表面にヒットすると、DXR はヒットにいくつかの属性を提供できます。
  // このサンプルでは、​​三角形の最後の 2 つの頂点の重み u、v によって定義された重心座標を使用するだけです。
  // 実際の重心座標は、float3 barycentrics = float3(1.f-u-v, u, v); を使用して取得できます。
  pipeline.SetMaxAttributeSize(2 * sizeof(float)); // barycentric coordinates(重心座標)

  // The raytracing process can shoot rays from existing hit points, resulting in nested TraceRay calls.
  // レイトレーシングプロセスは、既存のヒットポイントからレイを発射でき、ネストされた TraceRay 呼び出しが発生します。
  // Our sample code traces only primary rays, which then requires a trace depth of 1.
  // サンプルコードはプライマリレイのみをトレースするため、トレース深度1が必要です。
  // Note that this recursion depth should bekept to a minimum for best performance.
  // 最高のパフォーマンスを得るには、この再帰の深さを最小限に抑える必要があることに注意してください。
  // Path tracing algorithms can be easily flattened into a simple loop in the ray generation.
  // パストレースアルゴリズムは、レイ生成の単純なループに簡単にフラット化できます。
  pipeline.SetMaxRecursionDepth(1);

  // Compile the pipeline for execution on the GPU
  // GPU で実行するためにパイプラインをコンパイルします
  m_rtStateObject = pipeline.Generate();

  // Cast the state object into a properties object, allowing to later access the shader pointers by name
  // 状態オブジェクトをプロパティ オブジェクトにキャストし、後で名前でシェーダー ポインターにアクセスできるようにします
  ThrowIfFailed(
      m_rtStateObject->QueryInterface(IID_PPV_ARGS(&m_rtStateObjectProps)));
}

//-----------------------------------------------------------------------------
//
// Allocate the buffer holding the raytracing output, with the same size as the output image
// 出力画像と同じサイズのレイトレーシング出力を保持するバッファを割り当てます
void D3D12HelloTriangle::CreateRaytracingOutputBuffer() {
  D3D12_RESOURCE_DESC resDesc = {};
  resDesc.DepthOrArraySize = 1;
  resDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
  // The backbuffer is actually DXGI_FORMAT_R8G8B8A8_UNORM_SRGB, but sRGB formats cannot be used with UAVs. 
  // バックバッファは実際にはDXGI_FORMAT_R8G8B8A8_UNORM_SRGB ですが、sRGBフォーマットはUAVでは使用できません。
  // For accuracy we should convert to sRGB ourselves in the shader
  // 正確にするために、シェーダーで自分自身で sRGB に変換する必要があります
  resDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;

  resDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
  resDesc.Width = GetWidth();
  resDesc.Height = GetHeight();
  resDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
  resDesc.MipLevels = 1;
  resDesc.SampleDesc.Count = 1;
  ThrowIfFailed(m_device->CreateCommittedResource(
      &nv_helpers_dx12::kDefaultHeapProps, D3D12_HEAP_FLAG_NONE, &resDesc,
      D3D12_RESOURCE_STATE_COPY_SOURCE, nullptr,
      IID_PPV_ARGS(&m_outputResource)));
}

//-----------------------------------------------------------------------------
//
// Create the main heap used by the shaders,
// which will give access to the raytracing output and the top-level acceleration structure
// シェーダーが使用するメイン ヒープを作成します。これにより、レイトレーシング出力とトップレベルのアクセラレーション構造にアクセスできます。
void D3D12HelloTriangle::CreateShaderResourceHeap() {
  // Create a SRV/UAV/CBV descriptor heap. We need 2 entries - 1 UAV for the raytracing output and 1 SRV for the TLAS
  // SRV/UAV/CBV 記述子ヒープを作成します。2つのエントリが必要です - レイトレーシング出力用に1つのUAVとTLAS用に1つのSRV
  m_srvUavHeap = nv_helpers_dx12::CreateDescriptorHeap(
      m_device.Get(), 2, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV, true);

  // Get a handle to the heap memory on the CPU side, to be able to write the descriptors directly
  // CPU 側のヒープ メモリへのハンドルを取得して、記述子を直接書き込めるようにします
  D3D12_CPU_DESCRIPTOR_HANDLE srvHandle =
      m_srvUavHeap->GetCPUDescriptorHandleForHeapStart();

  // Create the UAV. Based on the root signature we created it is the first entry.
  // UAV を作成します。作成したルート署名に基づくと、これは最初のエントリです。
  // The Create*View methods write the view information directly into srvHandle
  // Create*View メソッドは、ビュー情報を srvHandle に直接書き込みます
  D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
  uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
  m_device->CreateUnorderedAccessView(m_outputResource.Get(), nullptr, &uavDesc,
                                      srvHandle);

  // Add the Top Level AS SRV right after the raytracing output buffer
  // レイトレーシング出力バッファの直後にトップ レベル AS SRV を追加します
  srvHandle.ptr += m_device->GetDescriptorHandleIncrementSize(
      D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

  D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc;
  srvDesc.Format = DXGI_FORMAT_UNKNOWN;
  srvDesc.ViewDimension = D3D12_SRV_DIMENSION_RAYTRACING_ACCELERATION_STRUCTURE;
  srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
  srvDesc.RaytracingAccelerationStructure.Location =
      m_topLevelASBuffers.pResult->GetGPUVirtualAddress();
  // Write the acceleration structure view in the heap
  // ヒープに加速構造ビューを書き込む
  m_device->CreateShaderResourceView(nullptr, &srvDesc, srvHandle);
}

//-----------------------------------------------------------------------------
//
// The Shader Binding Table (SBT) is the cornerstone of the raytracing setup:
// this is where the shader resources are bound to the shaders, in a way that
// can be interpreted by the raytracer on GPU. In terms of layout, the SBT
// contains a series of shader IDs with their resource pointers. The SBT
// contains the ray generation shader, the miss shaders, then the hit groups.
// Using the helper class, those can be specified in arbitrary order.
// シェーダー バインディング テーブル (SBT) は、レイトレーシング セットアップの基礎です。
// これは、シェーダー リソースが GPU のレイトレーシングによって解釈できる方法でシェーダーにバインドされる場所です。
// レイアウトに関しては、SBT には一連のシェーダー ID とそのリソース ポインターが含まれています。
// SBT には、レイ生成シェーダー、ミス シェーダー、ヒット グループが含まれます。ヘルパー クラスを使用して、任意の順序で指定できます。
void D3D12HelloTriangle::CreateShaderBindingTable() {
  // The SBT helper class collects calls to Add*Program. 
  // SBT ヘルパー クラスは、Add*Program への呼び出しを収集します。
  // If called several times, the helper must be emptied before re-adding shaders.
  // 数回呼び出された場合、シェーダーを再追加する前にヘルパーを空にする必要があります。
  m_sbtHelper.Reset();

  // The pointer to the beginning of the heap is the only parameter required by shaders without root parameters
  // ヒープの先頭へのポインターは、ルート パラメーターのないシェーダーに必要な唯一のパラメーターです。
  D3D12_GPU_DESCRIPTOR_HANDLE srvUavHeapHandle =
      m_srvUavHeap->GetGPUDescriptorHandleForHeapStart();

  // The helper treats both root parameter pointers and heap pointers as void*,
  // while DX12 uses the D3D12_GPU_DESCRIPTOR_HANDLE to define heap pointers.
  // ヘルパーはルート パラメーター ポインターとヒープ ポインターの両方を void* として扱いますが、
  // DX12 は D3D12_GPU_DESCRIPTOR_HANDLE を使用してヒープ ポインターを定義します。
  // The pointer in this struct is a UINT64, which then has to be reinterpreted as a pointer.
  // この構造体のポインタは UINT64 であり、ポインタとして再解釈する必要があります。

  auto heapPointer = reinterpret_cast<UINT64 *>(srvUavHeapHandle.ptr);

  // The ray generation only uses heap data
  // レイ生成はヒープ データのみを使用します
  m_sbtHelper.AddRayGenerationProgram(L"RayGen", {heapPointer});

  // The miss and hit shaders do not access any external resources: instead they
  // communicate their results through the ray payload
  // ミス シェーダーとヒット シェーダーは外部リソースにアクセスしません。代わりに、レイ ペイロードを介して結果を通信します。
  m_sbtHelper.AddMissProgram(L"Miss", {});

  // Adding the triangle hit shader
  // トライアングルヒットシェーダーの追加
  m_sbtHelper.AddHitGroup(L"HitGroup",
                          {(void *)(m_vertexBuffer->GetGPUVirtualAddress())});

  // Compute the size of the SBT given the number of shaders and their parameters
  // シェーダーとそのパラメーターの数を考慮して SBT のサイズを計算します

  uint32_t sbtSize = m_sbtHelper.ComputeSBTSize();

  // Create the SBT on the upload heap. This is required as the helper will use mapping to write the SBT contents.
  // アップロード ヒープに SBT を作成します。ヘルパーはマッピングを使用して SBT コンテンツを書き込むため、これは必須です。
  // After the SBT compilation it could be copied to the default heap for performance.
  // SBT のコンパイル後、パフォーマンスのためにデフォルトのヒープにコピーできます。
  m_sbtStorage = nv_helpers_dx12::CreateBuffer(
      m_device.Get(), sbtSize, D3D12_RESOURCE_FLAG_NONE,
      D3D12_RESOURCE_STATE_GENERIC_READ, nv_helpers_dx12::kUploadHeapProps);
  if (!m_sbtStorage) {
    throw std::logic_error("Could not allocate the shader binding table");
  }
  // Compile the SBT from the shader and parameters info
  // シェーダーとパラメーター情報から SBT をコンパイルします
  m_sbtHelper.Generate(m_sbtStorage.Get(), m_rtStateObjectProps.Get());
}
