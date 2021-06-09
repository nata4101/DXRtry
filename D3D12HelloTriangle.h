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

#pragma once

#include "DXSample.h"

#include <dxcapi.h>
#include <vector>

#include "nv_helpers_dx12/ShaderBindingTableGenerator.h"
#include "nv_helpers_dx12/TopLevelASGenerator.h"

using namespace DirectX;

// Note that while ComPtr is used to manage the lifetime of resources on the
// CPU, it has no understanding of the lifetime of resources on the GPU. Apps
// must account for the GPU lifetime of resources to avoid destroying objects
// that may still be referenced by the GPU. An example of this can be found in
// the class method: OnDestroy().
// ComPtr �́ACPU ��̃��\�[�X�̗L�����Ԃ��Ǘ����邽�߂Ɏg�p����܂����AGPU ��̃��\�[�X�̗L�����Ԃ𗝉����Ă��Ȃ����Ƃɒ��ӂ��Ă��������B
// �A�v���́AGPU �ɂ���Ĉ��������Q�Ƃ����\���̂���I�u�W�F�N�g�̔j����������邽�߂ɁA���\�[�X�� GPU ���C�t�^�C�����l������K�v������܂��B
// ���̗�́A�N���X ���\�b�h OnDestroy() �ɂ���܂��B
using Microsoft::WRL::ComPtr;

class D3D12HelloTriangle : public DXSample {
public:
  D3D12HelloTriangle(UINT width, UINT height, std::wstring name);

  virtual void OnInit();
  virtual void OnUpdate();
  virtual void OnRender();
  virtual void OnDestroy();

private:
  static const UINT FrameCount = 2;

  struct Vertex {
    XMFLOAT3 position;
    XMFLOAT4 color;
  };

  // Pipeline objects.
  //�p�C�v���C���I�u�W�F�N�g
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
  //�A�v�����\�[�X
  ComPtr<ID3D12Resource> m_vertexBuffer;
  D3D12_VERTEX_BUFFER_VIEW m_vertexBufferView;

  // Synchronization objects.
  // �����I�u�W�F�N�g�B
  UINT m_frameIndex;
  HANDLE m_fenceEvent;
  ComPtr<ID3D12Fence> m_fence;
  UINT64 m_fenceValue;

  void LoadPipeline();
  void LoadAssets();
  void PopulateCommandList();
  void WaitForPreviousFrame();

  void CheckRaytracingSupport();

  virtual void OnKeyUp(UINT8 key);
  bool m_raster = true;

  // #DXR
  struct AccelerationStructureBuffers {
    ComPtr<ID3D12Resource> pScratch;      // Scratch memory for AS builder(AS�r���_�[�p�X�N���b�`������)
    ComPtr<ID3D12Resource> pResult;       // Where the AS is(AS�̏ꏊ)
    ComPtr<ID3D12Resource> pInstanceDesc; // Hold the matrices of the instances(�C���X�^���X�̍s���ێ�����)
  };

  ComPtr<ID3D12Resource> m_bottomLevelAS; // Storage for the bottom Level AS(�ŉ��� AS �̃X�g���[�W)

  nv_helpers_dx12::TopLevelASGenerator m_topLevelASGenerator;
  AccelerationStructureBuffers m_topLevelASBuffers;
  std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>> m_instances;

  /// Create the acceleration structure of an instance(�C���X�^���X�̉����\�����쐬����)
  ///
  /// \param     vVertexBuffers : pair of buffer and vertex count
  /// �p�����[�^ vVertexBuffers: �o�b�t�@�ƒ��_���̃y�A
  /// \return    AccelerationStructureBuffers for TLAS
  /// �߂�l	 TLAS �� AccelerationStructureBuffers 
  AccelerationStructureBuffers CreateBottomLevelAS(
      std::vector<std::pair<ComPtr<ID3D12Resource>, uint32_t>> vVertexBuffers);

  /// Create the main acceleration structure that holds all instances of the scene
  /// �V�[���̂��ׂẴC���X�^���X��ێ����郁�C���̉����\�����쐬���܂�
  /// \param     instances : pair of BLAS and transform
  ///�p�����[�^�@�C���X�^���X: BLAS �ƕϊ��̃y�A
  void CreateTopLevelAS(
      const std::vector<std::pair<ComPtr<ID3D12Resource>, DirectX::XMMATRIX>>
          &instances);

  /// Create all acceleration structures, bottom and top
  /// �����Ə㕔�̂��ׂẲ����\�����쐬���܂�
  void CreateAccelerationStructures();

  // #DXR
  ComPtr<ID3D12RootSignature> CreateRayGenSignature();
  ComPtr<ID3D12RootSignature> CreateMissSignature();
  ComPtr<ID3D12RootSignature> CreateHitSignature();

  void CreateRaytracingPipeline();

  ComPtr<IDxcBlob> m_rayGenLibrary;
  ComPtr<IDxcBlob> m_hitLibrary;
  ComPtr<IDxcBlob> m_missLibrary;

  ComPtr<ID3D12RootSignature> m_rayGenSignature;
  ComPtr<ID3D12RootSignature> m_hitSignature;
  ComPtr<ID3D12RootSignature> m_missSignature;

  // Ray tracing pipeline state
  // ���C�g���[�V���O �p�C�v���C���̏��
  ComPtr<ID3D12StateObject> m_rtStateObject;
  // Ray tracing pipeline state properties, retaining the shader identifiers
  // to use in the Shader Binding Table
  // ���C �g���[�V���O �p�C�v���C���̏�ԃv���p�e�B�A�V�F�[�_�[ �o�C���f�B���O �e�[�u���Ŏg�p����V�F�[�_�[���ʎq��ێ�
  
  ComPtr<ID3D12StateObjectProperties> m_rtStateObjectProps;

  // #DXR
  void CreateRaytracingOutputBuffer();
  void CreateShaderResourceHeap();
  ComPtr<ID3D12Resource> m_outputResource;
  ComPtr<ID3D12DescriptorHeap> m_srvUavHeap;

  // #DXR
  void CreateShaderBindingTable();
  nv_helpers_dx12::ShaderBindingTableGenerator m_sbtHelper;
  ComPtr<ID3D12Resource> m_sbtStorage;
};
