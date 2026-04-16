/** @file Week4-6-ShapeComplete.cpp
 *  @brief Shape Practice Solution.
 *
 *  Place all of the scene geometry in one big vertex and index buffer.
 * Then use the DrawIndexedInstanced method to draw one object at a time ((as the
 * world matrix needs to be changed between objects)
 *
 *   Controls:
 *   Hold down '1' key to view scene in wireframe mode.
 *   Hold the left mouse button down and move the mouse to rotate.	
 *   Hold the right mouse button down and move the mouse to zoom in and out.
 *
 *  @author Hooman Salamat
 */


#include "../../Common/d3dApp.h"
#include "../../Common/MathHelper.h"
#include "../../Common/UploadBuffer.h"
#include "../../Common/GeometryGenerator.h"
#include "FrameResource.h"
#include "../../Common/DDSTextureLoader.h"
#include "../../Common/Camera.h"
#include <d3dcompiler.h> 
#pragma comment(lib, "d3dcompiler.lib")

using Microsoft::WRL::ComPtr;
using namespace DirectX;
using namespace DirectX::PackedVector;

const int gNumFrameResources = 3;

// Lightweight structure stores parameters to draw a shape.  This will
// vary from app-to-app.
struct RenderItem
{
	RenderItem() = default;

	// World matrix of the shape that describes the object's local space
	// relative to the world space, which defines the position, orientation,
	// and scale of the object in the world.
	XMFLOAT4X4 World = MathHelper::Identity4x4();

	// Dirty flag indicating the object data has changed and we need to update the constant buffer.
	// Because we have an object cbuffer for each FrameResource, we have to apply the
	// update to each FrameResource.  Thus, when we modify obect data we should set 
	// NumFramesDirty = gNumFrameResources so that each frame resource gets the update.
	int NumFramesDirty = gNumFrameResources;

	// Index into GPU constant buffer corresponding to the ObjectCB for this render item.
	UINT ObjCBIndex = -1;
	UINT TextureIndex = 0;

	MeshGeometry* Geo = nullptr;

	// Primitive topology.
	D3D12_PRIMITIVE_TOPOLOGY PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	// DrawIndexedInstanced parameters.
	UINT IndexCount = 0;
	UINT StartIndexLocation = 0;
	int BaseVertexLocation = 0;
};

enum class RenderLayer : int
{
	Opaque = 0,
	Transparent,
	AlphaTestedTreeSprites,
	Count
};

class ShapesApp : public D3DApp
{
public:
	ShapesApp(HINSTANCE hInstance);
	ShapesApp(const ShapesApp& rhs) = delete;
	ShapesApp& operator=(const ShapesApp& rhs) = delete;
	~ShapesApp();

	virtual bool Initialize()override;

private:
	virtual void OnResize()override;
	virtual void Update(const GameTimer& gt)override;
	virtual void Draw(const GameTimer& gt)override;

	virtual void OnMouseDown(WPARAM btnState, int x, int y)override;
	virtual void OnMouseUp(WPARAM btnState, int x, int y)override;
	virtual void OnMouseMove(WPARAM btnState, int x, int y)override;

	void OnKeyboardInput(const GameTimer& gt);
	void UpdateCamera(const GameTimer& gt);
	void UpdateObjectCBs(const GameTimer& gt);
	void UpdateMainPassCB(const GameTimer& gt);

	void BuildDescriptorHeaps();
	void BuildDescriptorHeapHelp();
	void BuildConstantBufferViews();
	void BuildRootSignature();
	void BuildShadersAndInputLayout();
	void BuildShapeGeometry();
	void BuildMazeGeometry();
	bool CheckCollision(const XMFLOAT3& newPosition, float radius = 0.5f);
	void BuildTreeSpritesGeometry();
	void BuildPSOs();
	void BuildFrameResources();
	void BuildRenderItems();
	void DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems);
	void LoadTextures();
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers();

private:
	struct Texture
	{
		std::string Name;
		std::wstring Filename;
		Microsoft::WRL::ComPtr<ID3D12Resource> Resource = nullptr;
		Microsoft::WRL::ComPtr<ID3D12Resource> UploadHeap = nullptr;
	};

	struct Wall
	{
		XMFLOAT3 Min;
		XMFLOAT3 Max;
	};

	std::vector<std::unique_ptr<FrameResource>> mFrameResources;
	FrameResource* mCurrFrameResource = nullptr;
	int mCurrFrameResourceIndex = 0;

	ComPtr<ID3D12RootSignature> mRootSignature = nullptr;
	ComPtr<ID3D12DescriptorHeap> mCbvHeap = nullptr;

	ComPtr<ID3D12DescriptorHeap> mSrvDescriptorHeap = nullptr;

	std::unordered_map<std::string, std::unique_ptr<Texture>> mTextures;
	std::unordered_map<std::string, std::unique_ptr<MeshGeometry>> mGeometries;
	std::unordered_map<std::string, ComPtr<ID3DBlob>> mShaders;
	std::unordered_map<std::string, ComPtr<ID3D12PipelineState>> mPSOs;

	std::vector<D3D12_INPUT_ELEMENT_DESC> mInputLayout;
	std::vector<D3D12_INPUT_ELEMENT_DESC> mTreeSpriteInputLayout;

	// List of all the render items.
	std::vector<std::unique_ptr<RenderItem>> mAllRitems;

	// Render items divided by PSO.
	std::vector<RenderItem*> mOpaqueRitems;
	std::vector<RenderItem*> mTransparentRitems;
	std::vector<RenderItem*> mRitemLayer[(int)RenderLayer::Count];
	std::vector<Wall> mMazeWalls;
	PassConstants mMainPassCB;

	UINT mPassCbvOffset = 0;

	bool mIsWireframe = false;

	Camera mCamera;
	XMFLOAT4X4 mProj = MathHelper::Identity4x4();

	POINT mLastMousePos;
};

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance,
	PSTR cmdLine, int showCmd)
{
	// Enable run-time memory check for debug builds.
#if defined(DEBUG) | defined(_DEBUG)
	_CrtSetDbgFlag(_CRTDBG_ALLOC_MEM_DF | _CRTDBG_LEAK_CHECK_DF);
#endif

	try
	{
		ShapesApp theApp(hInstance);
		if (!theApp.Initialize())
			return 0;

		return theApp.Run();
	}
	catch (DxException& e)
	{
		MessageBox(nullptr, e.ToString().c_str(), L"HR Failed", MB_OK);
		return 0;
	}
}

ShapesApp::ShapesApp(HINSTANCE hInstance)
	: D3DApp(hInstance)
{
}

ShapesApp::~ShapesApp()
{
	if (md3dDevice != nullptr)
		FlushCommandQueue();
}

bool ShapesApp::Initialize()
{
	if (!D3DApp::Initialize())
		return false;

	// Reset the command list to prep for initialization commands.
	ThrowIfFailed(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));
	
	LoadTextures();
	BuildRootSignature();
	BuildShadersAndInputLayout();
	BuildShapeGeometry();
	BuildTreeSpritesGeometry();
	BuildMazeGeometry();
	BuildRenderItems();
	BuildFrameResources();
	BuildDescriptorHeaps();
	BuildDescriptorHeapHelp();
	BuildConstantBufferViews();
	BuildPSOs();

	// Execute the initialization commands.
	ThrowIfFailed(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Wait until initialization is complete.
	FlushCommandQueue();
	mCamera.SetPosition(0.0f, 2.0f, -40.0f);
	mCamera.LookAt(
		XMFLOAT3(0.0f, 2.0f, -45.0f),
		XMFLOAT3(0.0f, 2.0f, -30.0f),
		XMFLOAT3(0.0f, 1.0f, 0.0f)
	);
	return true;
}

void ShapesApp::OnResize()
{
	D3DApp::OnResize();

	// The window resized, so update the aspect ratio and recompute the projection matrix.
	XMMATRIX P = XMMatrixPerspectiveFovLH(0.25f * MathHelper::Pi, AspectRatio(), 1.0f, 1000.0f);
	XMStoreFloat4x4(&mProj, P);
}

void ShapesApp::Update(const GameTimer& gt)
{
	OnKeyboardInput(gt);
	UpdateCamera(gt);

	// Cycle through the circular frame resource array.
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence)
	{
		HANDLE eventHandle = CreateEventEx(nullptr, nullptr, false, EVENT_ALL_ACCESS);
		ThrowIfFailed(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	UpdateObjectCBs(gt);
	UpdateMainPassCB(gt);
}

void ShapesApp::Draw(const GameTimer& gt)
{
	auto cmdListAlloc = mCurrFrameResource->CmdListAlloc;

	// Reuse the memory associated with command recording.
	// We can only reset when the associated command lists have finished execution on the GPU.
	ThrowIfFailed(cmdListAlloc->Reset());

	// A command list can be reset after it has been added to the command queue via ExecuteCommandList.
	// Reusing the command list reuses memory.
	if (mIsWireframe)
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque_wireframe"].Get()));
	}
	else
	{
		ThrowIfFailed(mCommandList->Reset(cmdListAlloc.Get(), mPSOs["opaque"].Get()));
	}
	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_PRESENT, D3D12_RESOURCE_STATE_RENDER_TARGET));

	// Clear the back buffer and depth buffer.
	mCommandList->ClearRenderTargetView(CurrentBackBufferView(), Colors::LightSteelBlue, 0, nullptr);
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	// Specify the buffers we are going to render to.
	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, &DepthStencilView());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->SetGraphicsRootSignature(mRootSignature.Get());

	UINT srvOffset = mPassCbvOffset + gNumFrameResources;
	CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	tex.Offset(srvOffset, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(2, tex);

	int passCbvIndex = mPassCbvOffset + mCurrFrameResourceIndex;
	auto passCbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
	passCbvHandle.Offset(passCbvIndex, mCbvSrvUavDescriptorSize);
	mCommandList->SetGraphicsRootDescriptorTable(1, passCbvHandle);

	DrawRenderItems(mCommandList.Get(), mOpaqueRitems);
	mCommandList->SetPipelineState(mPSOs["treeSprites"].Get());
	DrawRenderItems(mCommandList.Get(), mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites]);
	mCommandList->SetPipelineState(mPSOs["transparent"].Get());
	DrawRenderItems(mCommandList.Get(), mTransparentRitems);

	// Indicate a state transition on the resource usage.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(CurrentBackBuffer(),
		D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PRESENT));

	// Done recording commands.
	ThrowIfFailed(mCommandList->Close());

	// Add the command list to the queue for execution.
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	// Swap the back and front buffers
	ThrowIfFailed(mSwapChain->Present(0, 0));
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;

	// Advance the fence value to mark commands up to this fence point.
	mCurrFrameResource->Fence = ++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point. 
	// Because we are on the GPU timeline, the new fence point won't be 
	// set until the GPU finishes processing all the commands prior to this Signal().
	mCommandQueue->Signal(mFence.Get(), mCurrentFence);
}

void ShapesApp::OnMouseDown(WPARAM btnState, int x, int y)
{
	mLastMousePos.x = x;
	mLastMousePos.y = y;

	SetCapture(mhMainWnd);
}

void ShapesApp::OnMouseUp(WPARAM btnState, int x, int y)
{
	ReleaseCapture();
}

void ShapesApp::OnMouseMove(WPARAM btnState, int x, int y)
{
	if ((btnState & MK_LBUTTON) != 0)
	{
		float dx = XMConvertToRadians(0.25f * static_cast<float>(x - mLastMousePos.x));
		float dy = XMConvertToRadians(0.25f * static_cast<float>(y - mLastMousePos.y));

		mCamera.RotateY(dx);
		mCamera.Pitch(dy);
	}

	mLastMousePos.x = x;
	mLastMousePos.y = y;
}

void ShapesApp::OnKeyboardInput(const GameTimer& gt)
{
	if (GetAsyncKeyState('1') & 0x8000)
		mIsWireframe = true;
	else
		mIsWireframe = false;
}

void ShapesApp::UpdateCamera(const GameTimer& gt)
{
	const float dt = gt.DeltaTime();
	const float speed = 5.0f;

	XMFLOAT3 oldPos = mCamera.GetPosition3f();

	if (GetAsyncKeyState('W') & 0x8000 || GetAsyncKeyState(VK_UP) & 0x8000)
		mCamera.Walk(speed * dt);

	if (GetAsyncKeyState('S') & 0x8000 || GetAsyncKeyState(VK_DOWN) & 0x8000)
		mCamera.Walk(-speed * dt);

	if (GetAsyncKeyState('A') & 0x8000 || GetAsyncKeyState(VK_LEFT) & 0x8000)
		mCamera.Strafe(-speed * dt);

	if (GetAsyncKeyState('D') & 0x8000 || GetAsyncKeyState(VK_RIGHT) & 0x8000)
		mCamera.Strafe(speed * dt);

	XMFLOAT3 newPos = mCamera.GetPosition3f();
	newPos.y = 2.0f;

	if (CheckCollision(newPos, 0.5f))
	{
		oldPos.y = 2.0f;
		mCamera.SetPosition(oldPos);
	}
	else
	{
		mCamera.SetPosition(newPos);
	}
	mCamera.UpdateViewMatrix();
}

void ShapesApp::UpdateObjectCBs(const GameTimer& gt)
{
	auto currObjectCB = mCurrFrameResource->ObjectCB.get();
	for (auto& e : mAllRitems)
	{
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0)
		{
			XMMATRIX world = XMLoadFloat4x4(&e->World);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));

			currObjectCB->CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}
}

void ShapesApp::UpdateMainPassCB(const GameTimer& gt)
{
	XMMATRIX view = mCamera.GetView();
	XMMATRIX proj = mCamera.GetProj();
	mMainPassCB.EyePosW = mCamera.GetPosition3f();

	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB.View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB.InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB.Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB.InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB.ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB.InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB.RenderTargetSize = XMFLOAT2((float)mClientWidth, (float)mClientHeight);
	mMainPassCB.InvRenderTargetSize = XMFLOAT2(1.0f / mClientWidth, 1.0f / mClientHeight);
	mMainPassCB.NearZ = 1.0f;
	mMainPassCB.FarZ = 1000.0f;
	mMainPassCB.TotalTime = gt.TotalTime();
	mMainPassCB.DeltaTime = gt.DeltaTime();

	mMainPassCB.AmbientLight = XMFLOAT4(0.2f, 0.2f, 0.25f, 1.0f);
	mMainPassCB.DirectionalLightDirection = XMFLOAT3(0.57735f, -0.57735f, 0.57735f);
	mMainPassCB.DirectionalLightColor = XMFLOAT3(0.8f, 0.7f, 0.5f);
	mMainPassCB.NumPointLights = 10;

	mMainPassCB.PointLightPositions[0] = XMFLOAT4(-5.0f, 30.0f, -10.0f, 0.0f);
	mMainPassCB.PointLightColors[0] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[0] = 40.0f;

	mMainPassCB.PointLightPositions[1] = XMFLOAT4(5.0f, 30.0f, -10.0f, 0.0f);
	mMainPassCB.PointLightColors[1] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[1] = 40.0f;

	mMainPassCB.PointLightPositions[2] = XMFLOAT4(-5.0f, 30.0f, -5.0f, 0.0f);
	mMainPassCB.PointLightColors[2] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[2] = 40.0f;

	mMainPassCB.PointLightPositions[3] = XMFLOAT4(5.0f, 30.0f, -5.0f, 0.0f);
	mMainPassCB.PointLightColors[3] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[3] = 40.0f;

	mMainPassCB.PointLightPositions[4] = XMFLOAT4(-5.0f, 30.0f, 0.0f, 0.0f);
	mMainPassCB.PointLightColors[4] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[4] = 40.0f;

	mMainPassCB.PointLightPositions[5] = XMFLOAT4(5.0f, 30.0f, 0.0f, 0.0f);
	mMainPassCB.PointLightColors[5] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[5] = 40.0f;

	mMainPassCB.PointLightPositions[6] = XMFLOAT4(-5.0f, 30.0f, 5.0f, 0.0f);
	mMainPassCB.PointLightColors[6] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[6] = 40.0f;

	mMainPassCB.PointLightPositions[7] = XMFLOAT4(5.0f, 30.0f, 5.0f, 0.0f);
	mMainPassCB.PointLightColors[7] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[7] = 40.0f;

	mMainPassCB.PointLightPositions[8] = XMFLOAT4(-5.0f, 30.0f, 10.0f, 0.0f);
	mMainPassCB.PointLightColors[8] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[8] = 40.0f;

	mMainPassCB.PointLightPositions[9] = XMFLOAT4(5.0f, 30.0f, 10.0f, 0.0f);
	mMainPassCB.PointLightColors[9] = XMFLOAT4(5.0f, 1.0f, 0.5f, 0.0f);
	mMainPassCB.PointLightRanges[9] = 40.0f;

	auto currPassCB = mCurrFrameResource->PassCB.get();
	currPassCB->CopyData(0, mMainPassCB);
}

void ShapesApp::BuildDescriptorHeaps()
{
	D3D12_DESCRIPTOR_HEAP_DESC srvHeapDesc;
	srvHeapDesc.NumDescriptors = 1;
	srvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	srvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	srvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&srvHeapDesc,
		IID_PPV_ARGS(&mSrvDescriptorHeap)));

	UINT objCount = (UINT)mAllRitems.size();

	UINT numDescriptors = (objCount + 1) * gNumFrameResources + 100;

	mPassCbvOffset = objCount * gNumFrameResources;
	D3D12_DESCRIPTOR_HEAP_DESC cbvHeapDesc;
	cbvHeapDesc.NumDescriptors = numDescriptors;
	cbvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	cbvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	cbvHeapDesc.NodeMask = 0;
	ThrowIfFailed(md3dDevice->CreateDescriptorHeap(&cbvHeapDesc,
		IID_PPV_ARGS(&mCbvHeap)));
}

void ShapesApp::BuildDescriptorHeapHelp()
{
	UINT srvOffset = mPassCbvOffset + gNumFrameResources;
	CD3DX12_CPU_DESCRIPTOR_HANDLE hDescriptor(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
	hDescriptor.Offset(srvOffset, mCbvSrvUavDescriptorSize);

	auto stoneTexture = mTextures["stoneTexture"]->Resource;
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.Format = stoneTexture->GetDesc().Format;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = stoneTexture->GetDesc().MipLevels;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.PlaneSlice = 0;
	md3dDevice->CreateShaderResourceView(stoneTexture.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto brickTexture = mTextures["brickTexture"]->Resource;
	srvDesc.Format = brickTexture->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = brickTexture->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(brickTexture.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto tileTexture = mTextures["tileTexture"]->Resource;
	srvDesc.Format = tileTexture->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tileTexture->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tileTexture.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto treeArrayTex = mTextures["treeArrayTex"]->Resource;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2DARRAY;
	srvDesc.Format = treeArrayTex->GetDesc().Format;
	srvDesc.Texture2DArray.MostDetailedMip = 0;
	srvDesc.Texture2DArray.MipLevels = -1;
	srvDesc.Texture2DArray.FirstArraySlice = 0;
	srvDesc.Texture2DArray.ArraySize = treeArrayTex->GetDesc().DepthOrArraySize;
	md3dDevice->CreateShaderResourceView(treeArrayTex.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto tex4 = mTextures["tex4"]->Resource;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = tex4->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tex4->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tex4.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto tex5 = mTextures["tex5"]->Resource;
	srvDesc.Format = tex5->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tex5->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tex5.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto tex6 = mTextures["tex6"]->Resource;
	srvDesc.Format = tex6->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tex6->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tex6.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto tex7 = mTextures["tex7"]->Resource;
	srvDesc.Format = tex7->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tex7->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tex7.Get(), &srvDesc, hDescriptor);

	hDescriptor.Offset(1, mCbvSrvUavDescriptorSize);
	auto tex8 = mTextures["tex8"]->Resource;
	srvDesc.Format = tex8->GetDesc().Format;
	srvDesc.Texture2D.MipLevels = tex8->GetDesc().MipLevels;
	md3dDevice->CreateShaderResourceView(tex8.Get(), &srvDesc, hDescriptor);
}

void ShapesApp::BuildConstantBufferViews()
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));

	UINT objCount = (UINT)mAllRitems.size();

	// Need a CBV descriptor for each object for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto objectCB = mFrameResources[frameIndex]->ObjectCB->Resource();
		for (UINT i = 0; i < objCount; ++i)
		{
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress = objectCB->GetGPUVirtualAddress();

			// Offset to the ith object constant buffer in the buffer.
			cbAddress += i * objCBByteSize;

			// Offset to the object cbv in the descriptor heap.
			int heapIndex = frameIndex * objCount + i;
			auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
			handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

			D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
			cbvDesc.BufferLocation = cbAddress;
			cbvDesc.SizeInBytes = objCBByteSize;

			md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
		}
	}

	UINT passCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(PassConstants));

	// Last three descriptors are the pass CBVs for each frame resource.
	for (int frameIndex = 0; frameIndex < gNumFrameResources; ++frameIndex)
	{
		auto passCB = mFrameResources[frameIndex]->PassCB->Resource();
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress = passCB->GetGPUVirtualAddress();

		// Offset to the pass cbv in the descriptor heap.
		int heapIndex = mPassCbvOffset + frameIndex;
		auto handle = CD3DX12_CPU_DESCRIPTOR_HANDLE(mCbvHeap->GetCPUDescriptorHandleForHeapStart());
		handle.Offset(heapIndex, mCbvSrvUavDescriptorSize);

		D3D12_CONSTANT_BUFFER_VIEW_DESC cbvDesc;
		cbvDesc.BufferLocation = cbAddress;
		cbvDesc.SizeInBytes = passCBByteSize;

		md3dDevice->CreateConstantBufferView(&cbvDesc, handle);
	}
}

void ShapesApp::BuildRootSignature()
{
	CD3DX12_DESCRIPTOR_RANGE texTable;
	texTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable0;
	cbvTable0.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 0);

	CD3DX12_DESCRIPTOR_RANGE cbvTable1;
	cbvTable1.Init(D3D12_DESCRIPTOR_RANGE_TYPE_CBV, 1, 1);

	CD3DX12_ROOT_PARAMETER slotRootParameter[3];
	slotRootParameter[0].InitAsDescriptorTable(1, &cbvTable0);
	slotRootParameter[1].InitAsDescriptorTable(1, &cbvTable1);
	slotRootParameter[2].InitAsDescriptorTable(1, &texTable, D3D12_SHADER_VISIBILITY_PIXEL);

	auto staticSamplers = GetStaticSamplers();

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(3, slotRootParameter,
		(UINT)staticSamplers.size(), staticSamplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

	ComPtr<ID3DBlob> serializedRootSig = nullptr;
	ComPtr<ID3DBlob> errorBlob = nullptr;
	HRESULT hr = D3D12SerializeRootSignature(&rootSigDesc, D3D_ROOT_SIGNATURE_VERSION_1,
		serializedRootSig.GetAddressOf(), errorBlob.GetAddressOf());

	if (errorBlob != nullptr)
	{
		::OutputDebugStringA((char*)errorBlob->GetBufferPointer());
	}
	ThrowIfFailed(hr);

	ThrowIfFailed(md3dDevice->CreateRootSignature(
		0,
		serializedRootSig->GetBufferPointer(),
		serializedRootSig->GetBufferSize(),
		IID_PPV_ARGS(mRootSignature.GetAddressOf())));
}

std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> ShapesApp::GetStaticSamplers()
{
	static const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
		0,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	static const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
		1,
		D3D12_FILTER_MIN_MAG_MIP_POINT,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	static const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
		2,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP);

	static const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
		3,
		D3D12_FILTER_MIN_MAG_MIP_LINEAR,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP);

	static const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
		4,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		D3D12_TEXTURE_ADDRESS_MODE_WRAP,
		0.0f,
		8);

	static const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
		5,
		D3D12_FILTER_ANISOTROPIC,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		D3D12_TEXTURE_ADDRESS_MODE_CLAMP,
		0.0f,
		8);

	return {
		pointWrap, pointClamp,
		linearWrap, linearClamp,
		anisotropicWrap, anisotropicClamp };
}

void ShapesApp::BuildShadersAndInputLayout()
{
	mShaders["standardVS"] = d3dUtil::CompileShader(L"Shaders\\VS.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["opaquePS"] = d3dUtil::CompileShader(L"Shaders\\PS.hlsl", nullptr, "PS", "ps_5_1");

	mInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 28, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 36, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};

	const D3D_SHADER_MACRO alphaTestDefines[] =
	{
		"ALPHA_TEST", "1",
		NULL, NULL
	};

	mShaders["treeSpriteVS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "VS", "vs_5_1");
	mShaders["treeSpriteGS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", nullptr, "GS", "gs_5_1");
	mShaders["treeSpritePS"] = d3dUtil::CompileShader(L"Shaders\\TreeSprite.hlsl", alphaTestDefines, "PS", "ps_5_1");

	mTreeSpriteInputLayout =
	{
		{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,  D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "SIZE",     0, DXGI_FORMAT_R32G32_FLOAT,    0, 12, D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
	};
}
void ShapesApp::BuildShapeGeometry()
{
	GeometryGenerator geoGen;
	GeometryGenerator::MeshData box = geoGen.CreateBox(1.0f, 1.0f, 1.0f, 0);
	GeometryGenerator::MeshData grid = geoGen.CreateGrid(20.0f, 30.0f, 60, 40);
	GeometryGenerator::MeshData sphere = geoGen.CreateSphere(0.5f, 20, 20);
	GeometryGenerator::MeshData cylinder = geoGen.CreateCylinder(0.5f, 0.3f, 3.0f, 20, 20);

	GeometryGenerator::MeshData rectangle = geoGen.CreateRectangle(4.0f, 2.0f, 1.8f, 0);
	GeometryGenerator::MeshData triangularPrism = geoGen.CreateTriangularPrism(1.0f, 1.0f, 7.0f, 0);
	GeometryGenerator::MeshData cone = geoGen.CreateCone(0.5f, 5.0f, 20, 20);
	GeometryGenerator::MeshData diamond = geoGen.CreateDiamond(0.5f, 2.0f, 20, 20);
	GeometryGenerator::MeshData pyramid = geoGen.CreatePyramid(1.0f, 1.5f, 0);
	GeometryGenerator::MeshData torus = geoGen.CreateTorus(1.0f, 0.4f, 30, 30);
	GeometryGenerator::MeshData waterGrid = geoGen.CreateGrid(50.0f, 50.0f, 60, 40);

	// We are concatenating all the geometry into one big vertex/index buffer.  So
	// define the regions in the buffer each submesh covers.

	// Cache the vertex offsets to each object in the concatenated vertex buffer.
	UINT boxVertexOffset = 0;
	UINT gridVertexOffset = (UINT)box.Vertices.size();
	UINT sphereVertexOffset = gridVertexOffset + (UINT)grid.Vertices.size();
	UINT cylinderVertexOffset = sphereVertexOffset + (UINT)sphere.Vertices.size();

	UINT rectangleVertexOffset = cylinderVertexOffset + (UINT)cylinder.Vertices.size();
	UINT triangularPrismVertexOffset = rectangleVertexOffset + (UINT)rectangle.Vertices.size();
	UINT coneVertexOffset = triangularPrismVertexOffset + (UINT)triangularPrism.Vertices.size();
	UINT diamondVertexOffset = coneVertexOffset + (UINT)cone.Vertices.size();
	UINT pyramidVertexOffset = diamondVertexOffset + (UINT)diamond.Vertices.size();
	UINT torusVertexOffset = pyramidVertexOffset + (UINT)pyramid.Vertices.size();
	UINT waterGridVertexOffset = torusVertexOffset + (UINT)torus.Vertices.size();


	// Cache the starting index for each object in the concatenated index buffer.
	UINT boxIndexOffset = 0;
	UINT gridIndexOffset = (UINT)box.Indices32.size();
	UINT sphereIndexOffset = gridIndexOffset + (UINT)grid.Indices32.size();
	UINT cylinderIndexOffset = sphereIndexOffset + (UINT)sphere.Indices32.size();

	UINT rectangleIndexOffset = cylinderIndexOffset + (UINT)cylinder.Indices32.size();
	UINT triangularPrismIndexOffset = rectangleIndexOffset + (UINT)rectangle.Indices32.size();
	UINT coneIndexOffset = triangularPrismIndexOffset + (UINT)triangularPrism.Indices32.size();
	UINT diamondIndexOffset = coneIndexOffset + (UINT)cone.Indices32.size();
	UINT pyramidIndexOffset = diamondIndexOffset + (UINT)diamond.Indices32.size();
	UINT torusIndexOffset = pyramidIndexOffset + (UINT)pyramid.Indices32.size();
	UINT waterGridIndexOffset = torusIndexOffset + (UINT)torus.Indices32.size();


	// Define the SubmeshGeometry that cover different
	// regions of the vertex/index buffers.

	SubmeshGeometry boxSubmesh;
	boxSubmesh.IndexCount = (UINT)box.Indices32.size();
	boxSubmesh.StartIndexLocation = boxIndexOffset;
	boxSubmesh.BaseVertexLocation = boxVertexOffset;

	SubmeshGeometry gridSubmesh;
	gridSubmesh.IndexCount = (UINT)grid.Indices32.size();
	gridSubmesh.StartIndexLocation = gridIndexOffset;
	gridSubmesh.BaseVertexLocation = gridVertexOffset;

	SubmeshGeometry sphereSubmesh;
	sphereSubmesh.IndexCount = (UINT)sphere.Indices32.size();
	sphereSubmesh.StartIndexLocation = sphereIndexOffset;
	sphereSubmesh.BaseVertexLocation = sphereVertexOffset;

	SubmeshGeometry cylinderSubmesh;
	cylinderSubmesh.IndexCount = (UINT)cylinder.Indices32.size();
	cylinderSubmesh.StartIndexLocation = cylinderIndexOffset;
	cylinderSubmesh.BaseVertexLocation = cylinderVertexOffset;

	SubmeshGeometry rectangleSubmesh;
	rectangleSubmesh.IndexCount = (UINT)rectangle.Indices32.size();
	rectangleSubmesh.StartIndexLocation = rectangleIndexOffset;
	rectangleSubmesh.BaseVertexLocation = rectangleVertexOffset;

	SubmeshGeometry triangularPrismSubmesh;
	triangularPrismSubmesh.IndexCount = (UINT)triangularPrism.Indices32.size();
	triangularPrismSubmesh.StartIndexLocation = triangularPrismIndexOffset;
	triangularPrismSubmesh.BaseVertexLocation = triangularPrismVertexOffset;

	SubmeshGeometry coneSubmesh;
	coneSubmesh.IndexCount = (UINT)cone.Indices32.size();
	coneSubmesh.StartIndexLocation = coneIndexOffset;
	coneSubmesh.BaseVertexLocation = coneVertexOffset;

	SubmeshGeometry diamondSubmesh;
	diamondSubmesh.IndexCount = (UINT)diamond.Indices32.size();
	diamondSubmesh.StartIndexLocation = diamondIndexOffset;
	diamondSubmesh.BaseVertexLocation = diamondVertexOffset;

	SubmeshGeometry pyramidSubmesh;
	pyramidSubmesh.IndexCount = (UINT)pyramid.Indices32.size();
	pyramidSubmesh.StartIndexLocation = pyramidIndexOffset;
	pyramidSubmesh.BaseVertexLocation = pyramidVertexOffset;

	SubmeshGeometry torusSubmesh;
	torusSubmesh.IndexCount = (UINT)torus.Indices32.size();
	torusSubmesh.StartIndexLocation = torusIndexOffset;
	torusSubmesh.BaseVertexLocation = torusVertexOffset;

	SubmeshGeometry waterGridSubmesh;
	waterGridSubmesh.IndexCount = (UINT)waterGrid.Indices32.size();
	waterGridSubmesh.StartIndexLocation = waterGridIndexOffset;
	waterGridSubmesh.BaseVertexLocation = waterGridVertexOffset;

	// Extract the vertex elements we are interested in and pack the
	// vertices of all the meshes into one vertex buffer.

	auto totalVertexCount =
		box.Vertices.size() +
		grid.Vertices.size() +
		sphere.Vertices.size() +
		cylinder.Vertices.size() +
		rectangle.Vertices.size() +
		triangularPrism.Vertices.size() +
		cone.Vertices.size() +
		diamond.Vertices.size() +
		pyramid.Vertices.size() +
		torus.Vertices.size() +
		waterGrid.Vertices.size();


	std::vector<Vertex> vertices(totalVertexCount);

	UINT k = 0;

	for (size_t i = 0; i < box.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = box.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Gold);
		vertices[k].Tex = box.Vertices[i].TexC;
		vertices[k].Normal = box.Vertices[i].Normal;
	}

	for (size_t i = 0; i < grid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = grid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::ForestGreen);
		vertices[k].Tex = grid.Vertices[i].TexC;
		vertices[k].Normal = grid.Vertices[i].Normal;
	}

	for (size_t i = 0; i < sphere.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = sphere.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Crimson);
		vertices[k].Tex = sphere.Vertices[i].TexC;
		vertices[k].Normal = sphere.Vertices[i].Normal;
	}

	for (size_t i = 0; i < cylinder.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cylinder.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::SteelBlue);
		vertices[k].Tex = cylinder.Vertices[i].TexC;
		vertices[k].Normal = cylinder.Vertices[i].Normal;
	}

	for (size_t i = 0; i < rectangle.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = rectangle.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Purple);
		vertices[k].Tex = rectangle.Vertices[i].TexC;
		vertices[k].Normal = rectangle.Vertices[i].Normal;
	}

	for (size_t i = 0; i < triangularPrism.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = triangularPrism.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Orange);
		vertices[k].Tex = triangularPrism.Vertices[i].TexC;
		vertices[k].Normal = triangularPrism.Vertices[i].Normal;
	}

	for (size_t i = 0; i < cone.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = cone.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Yellow);
		vertices[k].Tex = cone.Vertices[i].TexC;
		vertices[k].Normal = cone.Vertices[i].Normal;
	}

	for (size_t i = 0; i < diamond.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = diamond.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Cyan);
		vertices[k].Tex = diamond.Vertices[i].TexC;
		vertices[k].Normal = diamond.Vertices[i].Normal;
	}

	for (size_t i = 0; i < pyramid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = pyramid.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::Pink);
		vertices[k].Tex = pyramid.Vertices[i].TexC;
		vertices[k].Normal = pyramid.Vertices[i].Normal;
	}

	for (size_t i = 0; i < torus.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = torus.Vertices[i].Position;
		vertices[k].Color = XMFLOAT4(DirectX::Colors::LightGreen);
		vertices[k].Tex = torus.Vertices[i].TexC;
		vertices[k].Normal = torus.Vertices[i].Normal;
	}

	for (size_t i = 0; i < waterGrid.Vertices.size(); ++i, ++k)
	{
		vertices[k].Pos = waterGrid.Vertices[i].Position;
		vertices[k].Pos.y = 0.3f;
		vertices[k].Color = XMFLOAT4(0.0f, 0.4f, 0.8f, 0.2f);
		vertices[k].Tex = waterGrid.Vertices[i].TexC;
		vertices[k].Normal = waterGrid.Vertices[i].Normal;
	}


	std::vector<std::uint16_t> indices;
	indices.insert(indices.end(), std::begin(box.GetIndices16()), std::end(box.GetIndices16()));
	indices.insert(indices.end(), std::begin(grid.GetIndices16()), std::end(grid.GetIndices16()));
	indices.insert(indices.end(), std::begin(sphere.GetIndices16()), std::end(sphere.GetIndices16()));
	indices.insert(indices.end(), std::begin(cylinder.GetIndices16()), std::end(cylinder.GetIndices16()));

	indices.insert(indices.end(), std::begin(rectangle.GetIndices16()), std::end(rectangle.GetIndices16()));
	indices.insert(indices.end(), std::begin(triangularPrism.GetIndices16()), std::end(triangularPrism.GetIndices16()));
	indices.insert(indices.end(), std::begin(cone.GetIndices16()), std::end(cone.GetIndices16()));
	indices.insert(indices.end(), std::begin(diamond.GetIndices16()), std::end(diamond.GetIndices16()));
	indices.insert(indices.end(), std::begin(pyramid.GetIndices16()), std::end(pyramid.GetIndices16()));
	indices.insert(indices.end(), std::begin(torus.GetIndices16()), std::end(torus.GetIndices16()));
	indices.insert(indices.end(), std::begin(waterGrid.GetIndices16()), std::end(waterGrid.GetIndices16()));

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "shapeGeo";


	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	geo->DrawArgs["box"] = boxSubmesh;
	geo->DrawArgs["grid"] = gridSubmesh;
	geo->DrawArgs["sphere"] = sphereSubmesh;
	geo->DrawArgs["cylinder"] = cylinderSubmesh;

	geo->DrawArgs["rectangle"] = rectangleSubmesh;
	geo->DrawArgs["triangularPrism"] = triangularPrismSubmesh;
	geo->DrawArgs["cone"] = coneSubmesh;
	geo->DrawArgs["diamond"] = diamondSubmesh;
	geo->DrawArgs["pyramid"] = pyramidSubmesh;
	geo->DrawArgs["torus"] = torusSubmesh;
	geo->DrawArgs["waterGrid"] = waterGridSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}
void ShapesApp::BuildTreeSpritesGeometry()
{
	struct TreeSpriteVertex
	{
		XMFLOAT3 Pos;
		XMFLOAT2 Size;
	};

	static const int treeCount = 16;
	std::array<TreeSpriteVertex, 16> vertices;

	float treePositions[16][2] = {
		{-8.0f, -8.0f}, { 8.0f, -8.0f}, {-8.0f,  8.0f}, { 8.0f,  8.0f},
		{-12.0f, 0.0f}, {12.0f,  0.0f}, { 0.0f,-12.0f}, { 0.0f, 12.0f},
		{-10.0f,-10.0f},{10.0f,-10.0f}, {-10.0f,10.0f}, {10.0f, 10.0f},
		{-6.0f, -14.0f},{6.0f, -14.0f}, {-6.0f, 14.0f}, { 6.0f, 14.0f}
	};

	for (UINT i = 0; i < treeCount; ++i)
	{
		vertices[i].Pos = XMFLOAT3(treePositions[i][0], 2.0f, treePositions[i][1]);
		vertices[i].Size = XMFLOAT2(5.0f, 5.0f);
	}

	std::array<std::uint16_t, 16> indices =
	{
		0, 1, 2, 3, 4, 5, 6, 7,
		8, 9, 10, 11, 12, 13, 14, 15
	};

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(TreeSpriteVertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "treeSpritesGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);

	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(TreeSpriteVertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry submesh;
	submesh.IndexCount = (UINT)indices.size();
	submesh.StartIndexLocation = 0;
	submesh.BaseVertexLocation = 0;
	geo->DrawArgs["points"] = submesh;

	mGeometries["treeSpritesGeo"] = std::move(geo);
}

void ShapesApp::BuildMazeGeometry()
{
	GeometryGenerator geoGen;

	int mazeLayout[10][10] = {
		{1,1,1,0,1,1,1,1,1,1},
		{1,0,0,0,0,0,0,0,0,1},
		{1,0,1,1,1,1,1,1,0,1},
		{1,0,0,0,0,0,0,1,0,1},
		{1,1,1,0,1,1,0,1,0,1},
		{1,0,0,0,1,0,0,0,0,1},
		{1,0,1,1,1,0,1,1,1,1},
		{1,0,0,0,0,0,0,0,0,1},
		{1,0,0,0,1,1,1,1,0,1},
		{1,1,1,1,1,1,1,1,0,1}
	};

	const float cellSize = 2.0f;
	const float wallHeight = 2.0f;
	const float wallThickness = 1.0f;
	const float originX = -5.0f * cellSize;
	const float originZ = -5.0f * cellSize + -30.0f;

	std::vector<Vertex> vertices;
	std::vector<std::uint16_t> indices;
	UINT baseVertex = 0;

	bool usedHoriz[10][10] = {};
	bool usedVert[10][10] = {};

	auto addWallSegment = [&](float cx, float cz, float scaleX, float scaleZ)
		{
			GeometryGenerator::MeshData box =
				geoGen.CreateBox(scaleX, wallHeight, scaleZ, 0);

			for (auto& v : box.Vertices)
			{
				Vertex vert;
				vert.Pos = { v.Position.x + cx, v.Position.y + 1.0f, v.Position.z + cz };
				vert.Color = { 0.5f, 0.5f, 0.5f, 1.0f };
				vert.Tex = v.TexC;
				vert.Normal = v.Normal;
				vertices.push_back(vert);
			}

			for (auto idx : box.Indices32)
				indices.push_back(baseVertex + (std::uint16_t)idx);

			baseVertex += (UINT)box.Vertices.size();

			Wall w;
			w.Min = { cx - scaleX * 0.5f, 0.0f, cz - scaleZ * 0.5f };
			w.Max = { cx + scaleX * 0.5f, wallHeight + 1.0f, cz + scaleZ * 0.5f };
			mMazeWalls.push_back(w);
		};

	for (int row = 0; row < 10; ++row)
	{
		int col = 0;
		while (col < 10)
		{
			if (mazeLayout[row][col] == 1 && !usedHoriz[row][col])
			{
				int runEnd = col;
				while (runEnd + 1 < 10 && mazeLayout[row][runEnd + 1] == 1)
					++runEnd;

				int runLen = runEnd - col + 1;

				if (runLen > 1)
				{
					float cx = originX + (col + runEnd) * 0.5f * cellSize;
					float cz = originZ + row * cellSize;
					float scaleX = runLen * cellSize;
					float scaleZ = wallThickness;

					addWallSegment(cx, cz, scaleX, scaleZ);

					for (int c = col; c <= runEnd; ++c)
						usedHoriz[row][c] = true;

					col = runEnd + 1;
				}
				else
				{
					++col;
				}
			}
			else
			{
				++col;
			}
		}
	}

	for (int col = 0; col < 10; ++col)
	{
		int row = 0;
		while (row < 10)
		{
			if (mazeLayout[row][col] == 1 && !usedVert[row][col])
			{
				int runEnd = row;
				while (runEnd + 1 < 10 && mazeLayout[runEnd + 1][col] == 1)
					++runEnd;

				int runLen = runEnd - row + 1;

				if (runLen > 1)
				{
					float cx = originX + col * cellSize;
					float cz = originZ + (row + runEnd) * 0.5f * cellSize;
					float scaleX = wallThickness;
					float scaleZ = runLen * cellSize;

					addWallSegment(cx, cz, scaleX, scaleZ);

					for (int r = row; r <= runEnd; ++r)
						usedVert[r][col] = true;

					row = runEnd + 1;
				}
				else
				{
					++row;
				}
			}
			else
			{
				++row;
			}
		}
	}

	for (int row = 0; row < 10; ++row)
		for (int col = 0; col < 10; ++col)
			if (mazeLayout[row][col] == 1 && !usedHoriz[row][col] && !usedVert[row][col])
			{
				float cx = originX + col * cellSize;
				float cz = originZ + row * cellSize;
				addWallSegment(cx, cz, wallThickness, wallThickness);
			}

	const UINT vbByteSize = (UINT)vertices.size() * sizeof(Vertex);
	const UINT ibByteSize = (UINT)indices.size() * sizeof(std::uint16_t);

	auto geo = std::make_unique<MeshGeometry>();
	geo->Name = "mazeGeo";

	ThrowIfFailed(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
	CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);
	ThrowIfFailed(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
	CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

	geo->VertexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), vertices.data(), vbByteSize, geo->VertexBufferUploader);
	geo->IndexBufferGPU = d3dUtil::CreateDefaultBuffer(md3dDevice.Get(),
		mCommandList.Get(), indices.data(), ibByteSize, geo->IndexBufferUploader);

	geo->VertexByteStride = sizeof(Vertex);
	geo->VertexBufferByteSize = vbByteSize;
	geo->IndexFormat = DXGI_FORMAT_R16_UINT;
	geo->IndexBufferByteSize = ibByteSize;

	SubmeshGeometry mazeSubmesh;
	mazeSubmesh.IndexCount = (UINT)indices.size();
	mazeSubmesh.StartIndexLocation = 0;
	mazeSubmesh.BaseVertexLocation = 0;
	geo->DrawArgs["maze"] = mazeSubmesh;

	mGeometries[geo->Name] = std::move(geo);
}
bool ShapesApp::CheckCollision(const XMFLOAT3& newPosition, float radius)
{
	for (const auto& wall : mMazeWalls)
	{
		bool collisionX = newPosition.x + radius >= wall.Min.x &&
			newPosition.x - radius <= wall.Max.x;
		bool collisionY = newPosition.y + radius >= wall.Min.y &&
			newPosition.y - radius <= wall.Max.y;
		bool collisionZ = newPosition.z + radius >= wall.Min.z &&
			newPosition.z - radius <= wall.Max.z;
		if (collisionX && collisionY && collisionZ)
			return true;
	}
	return false;
}

void ShapesApp::LoadTextures()
{
	auto stoneTexture = std::make_unique<Texture>();
	stoneTexture->Name = "stoneTexture";
	stoneTexture->Filename = L"Textures/pdtextures/HiCompress/461223104.dds";
	HRESULT hr = DirectX::CreateDDSTextureFromFile12(
		md3dDevice.Get(),
		mCommandList.Get(),
		stoneTexture->Filename.c_str(),
		stoneTexture->Resource,
		stoneTexture->UploadHeap);
	if (FAILED(hr))
	{
		MessageBox(0, L"Failed to load texture!", 0, 0);
		return;
	}
	ThrowIfFailed(hr);
	mTextures[stoneTexture->Name] = std::move(stoneTexture);

	auto brickTexture = std::make_unique<Texture>();
	brickTexture->Name = "brickTexture";
	brickTexture->Filename = L"Textures/pdtextures/HiCompress/461223161.dds";
	hr = DirectX::CreateDDSTextureFromFile12(
		md3dDevice.Get(),
		mCommandList.Get(),
		brickTexture->Filename.c_str(),
		brickTexture->Resource,
		brickTexture->UploadHeap);
	if (FAILED(hr))
	{
		MessageBox(0, L"Failed to load texture!", 0, 0);
		return;
	}
	ThrowIfFailed(hr);
	mTextures[brickTexture->Name] = std::move(brickTexture);

	auto tex4 = std::make_unique<Texture>();
	tex4->Name = "tex4";
	tex4->Filename = L"Textures/pdtextures/HiCompress/461223135.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
		tex4->Filename.c_str(), tex4->Resource, tex4->UploadHeap));
	mTextures[tex4->Name] = std::move(tex4);

	auto tex5 = std::make_unique<Texture>();
	tex5->Name = "tex5";
	tex5->Filename = L"Textures/pdtextures/HiCompress/461223196.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
		tex5->Filename.c_str(), tex5->Resource, tex5->UploadHeap));
	mTextures[tex5->Name] = std::move(tex5);

	auto tex6 = std::make_unique<Texture>();
	tex6->Name = "tex6";
	tex6->Filename = L"Textures/pdtextures/HiCompress/461223149.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
		tex6->Filename.c_str(), tex6->Resource, tex6->UploadHeap));
	mTextures[tex6->Name] = std::move(tex6);

	auto tex7 = std::make_unique<Texture>();
	tex7->Name = "tex7";
	tex7->Filename = L"Textures/pdtextures/HiCompress/461223113.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
		tex7->Filename.c_str(), tex7->Resource, tex7->UploadHeap));
	mTextures[tex7->Name] = std::move(tex7);

	auto tex8 = std::make_unique<Texture>();
	tex8->Name = "tex8";
	tex8->Filename = L"Textures/pdtextures/HiCompress/461223147.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(), mCommandList.Get(),
		tex8->Filename.c_str(), tex8->Resource, tex8->UploadHeap));
	mTextures[tex8->Name] = std::move(tex8);

	auto tileTexture = std::make_unique<Texture>();
	tileTexture->Name = "tileTexture";
	tileTexture->Filename = L"Textures/pdtextures/HiCompress/461223194.dds";
	hr = DirectX::CreateDDSTextureFromFile12(
		md3dDevice.Get(),
		mCommandList.Get(),
		tileTexture->Filename.c_str(),
		tileTexture->Resource,
		tileTexture->UploadHeap);
	if (FAILED(hr))
	{
		MessageBox(0, L"Failed to load tile texture!", 0, 0);
		return;
	}
	ThrowIfFailed(hr);
	mTextures[tileTexture->Name] = std::move(tileTexture);

	auto treeArrayTex = std::make_unique<Texture>();
	treeArrayTex->Name = "treeArrayTex";
	treeArrayTex->Filename = L"../../Textures/treeArray2.dds";
	ThrowIfFailed(DirectX::CreateDDSTextureFromFile12(md3dDevice.Get(),
		mCommandList.Get(), treeArrayTex->Filename.c_str(),
		treeArrayTex->Resource, treeArrayTex->UploadHeap));
	mTextures[treeArrayTex->Name] = std::move(treeArrayTex);
}

void ShapesApp::BuildPSOs()
{
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaquePsoDesc;

	// PSO for opaque objects.
	ZeroMemory(&opaquePsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	opaquePsoDesc.InputLayout = { mInputLayout.data(), (UINT)mInputLayout.size() };
	opaquePsoDesc.pRootSignature = mRootSignature.Get();

	opaquePsoDesc.VS =
	{
	 reinterpret_cast<BYTE*>(mShaders["standardVS"]->GetBufferPointer()),
	 mShaders["standardVS"]->GetBufferSize()
	};

	opaquePsoDesc.PS =
	{
	 reinterpret_cast<BYTE*>(mShaders["opaquePS"]->GetBufferPointer()),
	 mShaders["opaquePS"]->GetBufferSize()
	};

	opaquePsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	opaquePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_SOLID;
	opaquePsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	opaquePsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	opaquePsoDesc.SampleMask = UINT_MAX;
	opaquePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	opaquePsoDesc.NumRenderTargets = 1;
	opaquePsoDesc.RTVFormats[0] = mBackBufferFormat;
	opaquePsoDesc.SampleDesc.Count = m4xMsaaState ? 4 : 1;
	opaquePsoDesc.SampleDesc.Quality = m4xMsaaState ? (m4xMsaaQuality - 1) : 0;
	opaquePsoDesc.DSVFormat = mDepthStencilFormat;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaquePsoDesc, IID_PPV_ARGS(&mPSOs["opaque"])));

	// PSO for opaque wireframe objects.
	D3D12_GRAPHICS_PIPELINE_STATE_DESC opaqueWireframePsoDesc = opaquePsoDesc;
	opaqueWireframePsoDesc.RasterizerState.FillMode = D3D12_FILL_MODE_WIREFRAME;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&opaqueWireframePsoDesc, IID_PPV_ARGS(&mPSOs["opaque_wireframe"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC transparentPsoDesc = opaquePsoDesc;

	transparentPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	transparentPsoDesc.BlendState.RenderTarget[0].BlendEnable = TRUE;
	transparentPsoDesc.BlendState.RenderTarget[0].SrcBlend = D3D12_BLEND_SRC_ALPHA;
	transparentPsoDesc.BlendState.RenderTarget[0].DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
	transparentPsoDesc.BlendState.RenderTarget[0].BlendOp = D3D12_BLEND_OP_ADD;
	transparentPsoDesc.BlendState.RenderTarget[0].SrcBlendAlpha = D3D12_BLEND_ONE;
	transparentPsoDesc.BlendState.RenderTarget[0].DestBlendAlpha = D3D12_BLEND_ZERO;
	transparentPsoDesc.BlendState.RenderTarget[0].BlendOpAlpha = D3D12_BLEND_OP_ADD;
	transparentPsoDesc.BlendState.RenderTarget[0].RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;

	transparentPsoDesc.DepthStencilState.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ZERO;

	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&transparentPsoDesc, IID_PPV_ARGS(&mPSOs["transparent"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC treeSpritePsoDesc = opaquePsoDesc;
	treeSpritePsoDesc.VS = { reinterpret_cast<BYTE*>(mShaders["treeSpriteVS"]->GetBufferPointer()), mShaders["treeSpriteVS"]->GetBufferSize() };
	treeSpritePsoDesc.GS = { reinterpret_cast<BYTE*>(mShaders["treeSpriteGS"]->GetBufferPointer()), mShaders["treeSpriteGS"]->GetBufferSize() };
	treeSpritePsoDesc.PS = { reinterpret_cast<BYTE*>(mShaders["treeSpritePS"]->GetBufferPointer()), mShaders["treeSpritePS"]->GetBufferSize() };
	treeSpritePsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_POINT;
	treeSpritePsoDesc.InputLayout = { mTreeSpriteInputLayout.data(), (UINT)mTreeSpriteInputLayout.size() };
	treeSpritePsoDesc.RasterizerState.CullMode = D3D12_CULL_MODE_NONE;
	ThrowIfFailed(md3dDevice->CreateGraphicsPipelineState(&treeSpritePsoDesc, IID_PPV_ARGS(&mPSOs["treeSprites"])));
}


void ShapesApp::BuildFrameResources()
{
	for (int i = 0; i < gNumFrameResources; ++i)
	{
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(),
			1, (UINT)mAllRitems.size()));
	}
}



void ShapesApp::BuildRenderItems()
{
	auto boxRitem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&boxRitem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(0.0f, 0.5f, 0.0f));

	boxRitem->ObjCBIndex = 0;
	boxRitem->TextureIndex = 0;
	boxRitem->Geo = mGeometries["shapeGeo"].get();
	boxRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	boxRitem->IndexCount = boxRitem->Geo->DrawArgs["box"].IndexCount;
	boxRitem->StartIndexLocation = boxRitem->Geo->DrawArgs["box"].StartIndexLocation;
	boxRitem->BaseVertexLocation = boxRitem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(boxRitem));

	auto box2Ritem = std::make_unique<RenderItem>();

	XMStoreFloat4x4(&box2Ritem->World, XMMatrixScaling(2.0f, 2.0f, 2.0f) * XMMatrixTranslation(-4.0f, 0.5f, -4.0f));

	box2Ritem->ObjCBIndex = 1;
	box2Ritem->TextureIndex = 1;
	box2Ritem->Geo = mGeometries["shapeGeo"].get();
	box2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	box2Ritem->IndexCount = box2Ritem->Geo->DrawArgs["box"].IndexCount;
	box2Ritem->StartIndexLocation = box2Ritem->Geo->DrawArgs["box"].StartIndexLocation;
	box2Ritem->BaseVertexLocation = box2Ritem->Geo->DrawArgs["box"].BaseVertexLocation;
	mAllRitems.push_back(std::move(box2Ritem));

	auto gridRitem = std::make_unique<RenderItem>();

	gridRitem->World = MathHelper::Identity4x4();
	gridRitem->ObjCBIndex = 2;
	gridRitem->TextureIndex = 2;
	gridRitem->Geo = mGeometries["shapeGeo"].get();
	gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;

	gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
	gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
	gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;

	mAllRitems.push_back(std::move(gridRitem));

	UINT objCBIndex = 3;

	auto rectangleRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rectangleRitem->World, XMMatrixTranslation(-2.0f, 1.0f, 5.0f));
	rectangleRitem->ObjCBIndex = objCBIndex++;
	rectangleRitem->TextureIndex = 5;
	rectangleRitem->Geo = mGeometries["shapeGeo"].get();
	rectangleRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rectangleRitem->IndexCount = rectangleRitem->Geo->DrawArgs["rectangle"].IndexCount;
	rectangleRitem->StartIndexLocation = rectangleRitem->Geo->DrawArgs["rectangle"].StartIndexLocation;
	rectangleRitem->BaseVertexLocation = rectangleRitem->Geo->DrawArgs["rectangle"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rectangleRitem));

	auto rectangle1Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rectangle1Ritem->World, XMMatrixTranslation(-2.0f, 1.0f, 0.0f));
	rectangle1Ritem->ObjCBIndex = objCBIndex++;
	rectangle1Ritem->TextureIndex = 5;
	rectangle1Ritem->Geo = mGeometries["shapeGeo"].get();
	rectangle1Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rectangle1Ritem->IndexCount = rectangle1Ritem->Geo->DrawArgs["rectangle"].IndexCount;
	rectangle1Ritem->StartIndexLocation = rectangle1Ritem->Geo->DrawArgs["rectangle"].StartIndexLocation;
	rectangle1Ritem->BaseVertexLocation = rectangle1Ritem->Geo->DrawArgs["rectangle"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rectangle1Ritem));

	auto rectangle2Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rectangle2Ritem->World, XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(-3.0f, 1.0f, 2.0f));
	rectangle2Ritem->ObjCBIndex = objCBIndex++;
	rectangle2Ritem->TextureIndex = 5;
	rectangle2Ritem->Geo = mGeometries["shapeGeo"].get();
	rectangle2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rectangle2Ritem->IndexCount = rectangle2Ritem->Geo->DrawArgs["rectangle"].IndexCount;
	rectangle2Ritem->StartIndexLocation = rectangle2Ritem->Geo->DrawArgs["rectangle"].StartIndexLocation;
	rectangle2Ritem->BaseVertexLocation = rectangle2Ritem->Geo->DrawArgs["rectangle"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rectangle2Ritem));

	auto rectangle3Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rectangle3Ritem->World, XMMatrixTranslation(2.0f, 1.0f, 5.0f));
	rectangle3Ritem->ObjCBIndex = objCBIndex++;
	rectangle3Ritem->TextureIndex = 5;
	rectangle3Ritem->Geo = mGeometries["shapeGeo"].get();
	rectangle3Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rectangle3Ritem->IndexCount = rectangle3Ritem->Geo->DrawArgs["rectangle"].IndexCount;
	rectangle3Ritem->StartIndexLocation = rectangle3Ritem->Geo->DrawArgs["rectangle"].StartIndexLocation;
	rectangle3Ritem->BaseVertexLocation = rectangle3Ritem->Geo->DrawArgs["rectangle"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rectangle3Ritem));

	auto rectangle4Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rectangle4Ritem->World, XMMatrixRotationY(XM_PIDIV2) * XMMatrixTranslation(3.0f, 1.0f, 2.0f));
	rectangle4Ritem->ObjCBIndex = objCBIndex++;
	rectangle4Ritem->TextureIndex = 5;
	rectangle4Ritem->Geo = mGeometries["shapeGeo"].get();
	rectangle4Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rectangle4Ritem->IndexCount = rectangle4Ritem->Geo->DrawArgs["rectangle"].IndexCount;
	rectangle4Ritem->StartIndexLocation = rectangle4Ritem->Geo->DrawArgs["rectangle"].StartIndexLocation;
	rectangle4Ritem->BaseVertexLocation = rectangle4Ritem->Geo->DrawArgs["rectangle"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rectangle4Ritem));

	auto rectangle5Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&rectangle5Ritem->World, XMMatrixTranslation(2.0f, 1.0f, 0.0f));
	rectangle5Ritem->ObjCBIndex = objCBIndex++;
	rectangle5Ritem->TextureIndex = 5;
	rectangle5Ritem->Geo = mGeometries["shapeGeo"].get();
	rectangle5Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	rectangle5Ritem->IndexCount = rectangle5Ritem->Geo->DrawArgs["rectangle"].IndexCount;
	rectangle5Ritem->StartIndexLocation = rectangle5Ritem->Geo->DrawArgs["rectangle"].StartIndexLocation;
	rectangle5Ritem->BaseVertexLocation = rectangle5Ritem->Geo->DrawArgs["rectangle"].BaseVertexLocation;
	mAllRitems.push_back(std::move(rectangle5Ritem));

	auto triangularPrismRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&triangularPrismRitem->World, XMMatrixRotationY(XM_PIDIV2)* XMMatrixTranslation(0.0f, 1.0f,-3.0f));
	triangularPrismRitem->ObjCBIndex = objCBIndex++;
	triangularPrismRitem->TextureIndex = 5;
	triangularPrismRitem->Geo = mGeometries["shapeGeo"].get();
	triangularPrismRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	triangularPrismRitem->IndexCount = triangularPrismRitem->Geo->DrawArgs["triangularPrism"].IndexCount;
	triangularPrismRitem->StartIndexLocation = triangularPrismRitem->Geo->DrawArgs["triangularPrism"].StartIndexLocation;
	triangularPrismRitem->BaseVertexLocation = triangularPrismRitem->Geo->DrawArgs["triangularPrism"].BaseVertexLocation;
	mAllRitems.push_back(std::move(triangularPrismRitem));

	auto coneRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&coneRitem->World, XMMatrixTranslation(0.0f, 1.0f, 2.5f));
	coneRitem->ObjCBIndex = objCBIndex++;
	coneRitem->TextureIndex = 7;
	coneRitem->Geo = mGeometries["shapeGeo"].get();
	coneRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	coneRitem->IndexCount = coneRitem->Geo->DrawArgs["cone"].IndexCount;
	coneRitem->StartIndexLocation = coneRitem->Geo->DrawArgs["cone"].StartIndexLocation;
	coneRitem->BaseVertexLocation = coneRitem->Geo->DrawArgs["cone"].BaseVertexLocation;
	mAllRitems.push_back(std::move(coneRitem));

	auto diamondRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&diamondRitem->World, XMMatrixTranslation(0.0f, 3.4f, 5.0f));
	diamondRitem->ObjCBIndex = objCBIndex++;
	diamondRitem->TextureIndex = 7;
	diamondRitem->Geo = mGeometries["shapeGeo"].get();
	diamondRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	diamondRitem->IndexCount = diamondRitem->Geo->DrawArgs["diamond"].IndexCount;
	diamondRitem->StartIndexLocation = diamondRitem->Geo->DrawArgs["diamond"].StartIndexLocation;
	diamondRitem->BaseVertexLocation = diamondRitem->Geo->DrawArgs["diamond"].BaseVertexLocation;
	mAllRitems.push_back(std::move(diamondRitem));

	auto pyramidRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramidRitem->World, XMMatrixTranslation(-3.0f, 3.0f, 5.0f));
	pyramidRitem->ObjCBIndex = objCBIndex++;
	pyramidRitem->TextureIndex = 8;
	pyramidRitem->Geo = mGeometries["shapeGeo"].get();
	pyramidRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramidRitem->IndexCount = pyramidRitem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramidRitem->StartIndexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramidRitem->BaseVertexLocation = pyramidRitem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramidRitem));

	auto pyramid1Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramid1Ritem->World, XMMatrixTranslation(-3.0f, 3.0f, 0.0f));
	pyramid1Ritem->ObjCBIndex = objCBIndex++;
	pyramid1Ritem->TextureIndex = 8;
	pyramid1Ritem->Geo = mGeometries["shapeGeo"].get();
	pyramid1Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramid1Ritem->IndexCount = pyramid1Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramid1Ritem->StartIndexLocation = pyramid1Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramid1Ritem->BaseVertexLocation = pyramid1Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramid1Ritem));

	auto pyramid2Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramid2Ritem->World, XMMatrixTranslation(3.0f, 3.0f, 0.0f));
	pyramid2Ritem->ObjCBIndex = objCBIndex++;
	pyramid2Ritem->TextureIndex = 8;
	pyramid2Ritem->Geo = mGeometries["shapeGeo"].get();
	pyramid2Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramid2Ritem->IndexCount = pyramid2Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramid2Ritem->StartIndexLocation = pyramid2Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramid2Ritem->BaseVertexLocation = pyramid2Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramid2Ritem));

	auto pyramid3Ritem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&pyramid3Ritem->World, XMMatrixTranslation(3.0f, 3.0f, 5.0f));
	pyramid3Ritem->ObjCBIndex = objCBIndex++;
	pyramid3Ritem->TextureIndex = 8;
	pyramid3Ritem->Geo = mGeometries["shapeGeo"].get();
	pyramid3Ritem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	pyramid3Ritem->IndexCount = pyramid3Ritem->Geo->DrawArgs["pyramid"].IndexCount;
	pyramid3Ritem->StartIndexLocation = pyramid3Ritem->Geo->DrawArgs["pyramid"].StartIndexLocation;
	pyramid3Ritem->BaseVertexLocation = pyramid3Ritem->Geo->DrawArgs["pyramid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(pyramid3Ritem));

	auto torusRitem = std::make_unique<RenderItem>();
	XMStoreFloat4x4(&torusRitem->World, XMMatrixTranslation(0.0f, 3.4f, 5.0f));
	torusRitem->ObjCBIndex = objCBIndex++;
	torusRitem->TextureIndex = 4;
	torusRitem->Geo = mGeometries["shapeGeo"].get();
	torusRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	torusRitem->IndexCount = torusRitem->Geo->DrawArgs["torus"].IndexCount;
	torusRitem->StartIndexLocation = torusRitem->Geo->DrawArgs["torus"].StartIndexLocation;
	torusRitem->BaseVertexLocation = torusRitem->Geo->DrawArgs["torus"].BaseVertexLocation;
	mAllRitems.push_back(std::move(torusRitem));

	UINT treeTexSrvOffset = mPassCbvOffset + gNumFrameResources + 100;

	auto treeSpritesRitem = std::make_unique<RenderItem>();
	treeSpritesRitem->World = MathHelper::Identity4x4();
	treeSpritesRitem->ObjCBIndex = objCBIndex++;
	treeSpritesRitem->TextureIndex = 3;
	treeSpritesRitem->Geo = mGeometries["treeSpritesGeo"].get();
	treeSpritesRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_POINTLIST;
	treeSpritesRitem->IndexCount = treeSpritesRitem->Geo->DrawArgs["points"].IndexCount;
	treeSpritesRitem->StartIndexLocation = treeSpritesRitem->Geo->DrawArgs["points"].StartIndexLocation;
	treeSpritesRitem->BaseVertexLocation = treeSpritesRitem->Geo->DrawArgs["points"].BaseVertexLocation;
	mRitemLayer[(int)RenderLayer::AlphaTestedTreeSprites].push_back(treeSpritesRitem.get());
	mAllRitems.push_back(std::move(treeSpritesRitem));

	for (int i = 0; i < 5; ++i)
	{
		auto leftCylRitem = std::make_unique<RenderItem>();
		auto rightCylRitem = std::make_unique<RenderItem>();

		auto leftSphereRitem = std::make_unique<RenderItem>();
		auto rightSphereRitem = std::make_unique<RenderItem>();

		XMMATRIX leftCylWorld = XMMatrixTranslation(-5.0f, 1.5f, -10.0f + i * 5.0f);
		XMMATRIX rightCylWorld = XMMatrixTranslation(+5.0f, 1.5f, -10.0f + i * 5.0f);

		XMMATRIX leftSphereWorld = XMMatrixTranslation(-5.0f, 3.5f, -10.0f + i * 5.0f);
		XMMATRIX rightSphereWorld = XMMatrixTranslation(+5.0f, 3.5f, -10.0f + i * 5.0f);

		XMStoreFloat4x4(&leftCylRitem->World, rightCylWorld);

		leftCylRitem->ObjCBIndex = objCBIndex++;

		leftCylRitem->Geo = mGeometries["shapeGeo"].get();
		leftCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftCylRitem->IndexCount = leftCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		leftCylRitem->StartIndexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		leftCylRitem->BaseVertexLocation = leftCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&rightCylRitem->World, leftCylWorld);

		rightCylRitem->ObjCBIndex = objCBIndex++;

		rightCylRitem->Geo = mGeometries["shapeGeo"].get();
		rightCylRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightCylRitem->IndexCount = rightCylRitem->Geo->DrawArgs["cylinder"].IndexCount;
		rightCylRitem->StartIndexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].StartIndexLocation;
		rightCylRitem->BaseVertexLocation = rightCylRitem->Geo->DrawArgs["cylinder"].BaseVertexLocation;

		XMStoreFloat4x4(&leftSphereRitem->World, leftSphereWorld);

		leftSphereRitem->ObjCBIndex = objCBIndex++;
		leftSphereRitem->Geo = mGeometries["shapeGeo"].get();
		leftSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		leftSphereRitem->IndexCount = leftSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		leftSphereRitem->StartIndexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		leftSphereRitem->BaseVertexLocation = leftSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		XMStoreFloat4x4(&rightSphereRitem->World, rightSphereWorld);

		rightSphereRitem->ObjCBIndex = objCBIndex++;

		rightSphereRitem->Geo = mGeometries["shapeGeo"].get();
		rightSphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		rightSphereRitem->IndexCount = rightSphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		rightSphereRitem->StartIndexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		rightSphereRitem->BaseVertexLocation = rightSphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;

		mAllRitems.push_back(std::move(leftCylRitem));
		mAllRitems.push_back(std::move(rightCylRitem));
		mAllRitems.push_back(std::move(leftSphereRitem));
		mAllRitems.push_back(std::move(rightSphereRitem));
	}

	auto waterRitem = std::make_unique<RenderItem>();
	waterRitem->World = MathHelper::Identity4x4();
	waterRitem->ObjCBIndex = objCBIndex++;
	waterRitem->TextureIndex = 2;
	waterRitem->Geo = mGeometries["shapeGeo"].get();
	waterRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
	waterRitem->IndexCount = waterRitem->Geo->DrawArgs["waterGrid"].IndexCount;
	waterRitem->StartIndexLocation = waterRitem->Geo->DrawArgs["waterGrid"].StartIndexLocation;
	waterRitem->BaseVertexLocation = waterRitem->Geo->DrawArgs["waterGrid"].BaseVertexLocation;
	mAllRitems.push_back(std::move(waterRitem));

	auto mazeRitem = std::make_unique<RenderItem>();
	mazeRitem->World = MathHelper::Identity4x4();
	mazeRitem->ObjCBIndex = objCBIndex++;
	mazeRitem->TextureIndex = 1;
	mazeRitem->Geo = mGeometries["mazeGeo"].get();
	mazeRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;	
	mazeRitem->IndexCount = mazeRitem->Geo->DrawArgs["maze"].IndexCount;
	mazeRitem->StartIndexLocation = mazeRitem->Geo->DrawArgs["maze"].StartIndexLocation;
	mazeRitem->BaseVertexLocation = mazeRitem->Geo->DrawArgs["maze"].BaseVertexLocation;
	mAllRitems.push_back(std::move(mazeRitem));

	// All the render items are opaque.
	for (size_t i = 0; i < mAllRitems.size(); ++i)
	{
		if (i == mAllRitems.size() - 2)
		{
			mTransparentRitems.push_back(mAllRitems[i].get());
		}
		else
		{
			mOpaqueRitems.push_back(mAllRitems[i].get());
		}
	}
}

void ShapesApp::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems)
{
	UINT objCBByteSize = d3dUtil::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	auto objectCB = mCurrFrameResource->ObjectCB->Resource();
	// For each render item...	

	for (size_t i = 0; i < ritems.size(); ++i)
	{
		auto ri = ritems[i];
		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		UINT srvOffset = mPassCbvOffset + gNumFrameResources + ri->TextureIndex;
		CD3DX12_GPU_DESCRIPTOR_HANDLE tex(mCbvHeap->GetGPUDescriptorHandleForHeapStart());
		tex.Offset(srvOffset, mCbvSrvUavDescriptorSize);
		cmdList->SetGraphicsRootDescriptorTable(2, tex);

		// Offset to the CBV in the descriptor heap for this object and for this frame resource.

		UINT cbvIndex = mCurrFrameResourceIndex * (UINT)mAllRitems.size() + ri->ObjCBIndex;

		auto cbvHandle = CD3DX12_GPU_DESCRIPTOR_HANDLE(mCbvHeap->GetGPUDescriptorHandleForHeapStart());

		cbvHandle.Offset(cbvIndex, mCbvSrvUavDescriptorSize);

		cmdList->SetGraphicsRootDescriptorTable(0, cbvHandle);
		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}
























































//Secret plans for taking over the world here