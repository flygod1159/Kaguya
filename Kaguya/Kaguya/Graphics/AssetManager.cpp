#include "AssetManager.h"

using namespace DirectX;
using Microsoft::WRL::ComPtr;

void AssetManager::Initialize()
{
	AsyncImageLoader.SetDelegate(
		[&](auto pImage)
		{
			std::scoped_lock _(Mutex);

			ImageUploadQueue.push(std::move(pImage));
			ConditionVariable.notify_all();
		});

	AsyncMeshLoader.SetDelegate(
		[&](auto pMesh)
		{
			std::scoped_lock _(Mutex);

			MeshUploadQueue.push(std::move(pMesh));
			ConditionVariable.notify_all();
		});

	struct threadwrapper
	{
		static unsigned int WINAPI thunk(LPVOID lpParameter)
		{
			D3D12LinkedDevice*			 Device = RenderCore::Device->GetDevice();
			D3D12ResourceUploader uploader(Device);

			while (true)
			{
				std::unique_lock _(Mutex);
				ConditionVariable.wait(_);

				if (Quit)
				{
					break;
				}

				uploader.Begin();

				std::vector<std::shared_ptr<D3D12Resource>> TrackedScratchBuffers;

				// container to store assets after it has been uploaded to be added to the AssetCache
				std::vector<std::shared_ptr<Asset::Image>> uploadedImages;
				std::vector<std::shared_ptr<Asset::Mesh>>  uploadedMeshes;

				// Process Image
				{
					while (!ImageUploadQueue.empty())
					{
						std::shared_ptr<Asset::Image> assetImage = ImageUploadQueue.front();
						ImageUploadQueue.pop();

						const auto& Image	 = assetImage->Image;
						const auto& Metadata = Image.GetMetadata();

						DXGI_FORMAT format = Metadata.format;
						if (assetImage->Metadata.sRGB)
							format = DirectX::MakeSRGB(format);

						D3D12_RESOURCE_DESC resourceDesc = {};
						switch (Metadata.dimension)
						{
						case TEX_DIMENSION::TEX_DIMENSION_TEXTURE1D:
							resourceDesc = CD3DX12_RESOURCE_DESC::Tex1D(
								format,
								static_cast<UINT64>(Metadata.width),
								static_cast<UINT16>(Metadata.arraySize));
							break;

						case TEX_DIMENSION::TEX_DIMENSION_TEXTURE2D:
							resourceDesc = CD3DX12_RESOURCE_DESC::Tex2D(
								format,
								static_cast<UINT64>(Metadata.width),
								static_cast<UINT>(Metadata.height),
								static_cast<UINT16>(Metadata.arraySize),
								static_cast<UINT16>(Metadata.mipLevels));
							break;

						case TEX_DIMENSION::TEX_DIMENSION_TEXTURE3D:
							resourceDesc = CD3DX12_RESOURCE_DESC::Tex3D(
								format,
								static_cast<UINT64>(Metadata.width),
								static_cast<UINT>(Metadata.height),
								static_cast<UINT16>(Metadata.depth));
							break;
						}

						assetImage->Texture = D3D12Texture(RenderCore::Device->GetDevice(), resourceDesc, {});
						assetImage->SRV		= D3D12ShaderResourceView(RenderCore::Device->GetDevice());
						assetImage->Texture.CreateShaderResourceView(assetImage->SRV);

						std::vector<D3D12_SUBRESOURCE_DATA> subresources(Image.GetImageCount());
						const auto							pImages = Image.GetImages();
						for (size_t i = 0; i < Image.GetImageCount(); ++i)
						{
							subresources[i].RowPitch   = pImages[i].rowPitch;
							subresources[i].SlicePitch = pImages[i].slicePitch;
							subresources[i].pData	   = pImages[i].pixels;
						}

						uploader.Upload(subresources, assetImage->Texture);

						uploadedImages.push_back(assetImage);
					}
				}

				// Process Mesh
				{
					while (!MeshUploadQueue.empty())
					{
						std::shared_ptr<Asset::Mesh> assetMesh = MeshUploadQueue.front();
						MeshUploadQueue.pop();

						UINT64 vertexBufferSizeInBytes = assetMesh->Vertices.size() * sizeof(Vertex);
						UINT64 indexBufferSizeInBytes  = assetMesh->Indices.size() * sizeof(unsigned int);

						D3D12Buffer vertexBuffer = D3D12Buffer(
							Device,
							vertexBufferSizeInBytes,
							sizeof(Vertex),
							D3D12_HEAP_TYPE_DEFAULT,
							D3D12_RESOURCE_FLAG_NONE);
						D3D12Buffer indexBuffer = D3D12Buffer(
							Device,
							indexBufferSizeInBytes,
							sizeof(unsigned int),
							D3D12_HEAP_TYPE_DEFAULT,
							D3D12_RESOURCE_FLAG_NONE);

						// Upload vertex data
						{
							D3D12_SUBRESOURCE_DATA subresource = {};
							subresource.pData				   = assetMesh->Vertices.data();
							subresource.RowPitch			   = vertexBufferSizeInBytes;
							subresource.SlicePitch			   = vertexBufferSizeInBytes;

							uploader.Upload(subresource, vertexBuffer);
						}

						// Upload index data
						{
							D3D12_SUBRESOURCE_DATA subresource = {};
							subresource.pData				   = assetMesh->Indices.data();
							subresource.RowPitch			   = indexBufferSizeInBytes;
							subresource.SlicePitch			   = indexBufferSizeInBytes;

							uploader.Upload(subresource, indexBuffer);
						}

						assetMesh->VertexResource = std::move(vertexBuffer);
						assetMesh->IndexResource  = std::move(indexBuffer);

						for (const auto& submesh : assetMesh->Submeshes)
						{
							D3D12_RAYTRACING_GEOMETRY_DESC dxrGeometryDesc = {};
							dxrGeometryDesc.Type						   = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
							dxrGeometryDesc.Flags						   = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;
							dxrGeometryDesc.Triangles.Transform3x4		   = NULL;
							dxrGeometryDesc.Triangles.IndexFormat		   = DXGI_FORMAT_R32_UINT;
							dxrGeometryDesc.Triangles.VertexFormat		   = DXGI_FORMAT_R32G32B32_FLOAT;
							dxrGeometryDesc.Triangles.IndexCount		   = submesh.IndexCount;
							dxrGeometryDesc.Triangles.VertexCount		   = submesh.VertexCount;
							dxrGeometryDesc.Triangles.IndexBuffer = assetMesh->IndexResource.GetGpuVirtualAddress() +
																	submesh.StartIndexLocation * sizeof(unsigned int);
							dxrGeometryDesc.Triangles.VertexBuffer.StartAddress =
								assetMesh->VertexResource.GetGpuVirtualAddress() +
								submesh.BaseVertexLocation * sizeof(Vertex);
							dxrGeometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);

							assetMesh->Blas.AddGeometry(dxrGeometryDesc);
						}

						uploadedMeshes.push_back(assetMesh);
					}
				}

				uploader.End(true);

				for (auto& Image : uploadedImages)
				{
					std::string String = Image->Metadata.Path.string();
					UINT64		Hash   = CityHash64(String.data(), String.size());

					ImageCache.Create(Hash);
					auto Asset = ImageCache.Load(Hash);
					*Asset	   = std::move(*Image);
				}

				for (auto& Mesh : uploadedMeshes)
				{
					std::string String = Mesh->Metadata.Path.string();
					UINT64		Hash   = CityHash64(String.data(), String.size());

					MeshCache.Create(Hash);
					auto Asset = MeshCache.Load(Hash);
					*Asset	   = std::move(*Mesh);
				}
			}

			return EXIT_SUCCESS;
		}
	};

	Thread.reset(reinterpret_cast<HANDLE>(_beginthreadex(nullptr, 0, threadwrapper::thunk, nullptr, 0, nullptr)));
}

void AssetManager::Shutdown()
{
	Quit = true;
	ConditionVariable.notify_all();

	::WaitForSingleObject(Thread.get(), INFINITE);

	ImageCache.DestroyAll();
	MeshCache.DestroyAll();
}

void AssetManager::AsyncLoadImage(const std::filesystem::path& Path, bool sRGB)
{
	if (!std::filesystem::exists(Path))
	{
		return;
	}

	std::string String = Path.string();
	UINT64		Hash   = CityHash64(String.data(), String.size());
	if (ImageCache.Exist(Hash))
	{
		LOG_INFO("{} Exists", String);
		return;
	}

	Asset::ImageMetadata Metadata = { .Path = Path, .sRGB = sRGB };
	AsyncImageLoader.RequestAsyncLoad(1, &Metadata);
}

void AssetManager::AsyncLoadMesh(const std::filesystem::path& Path, bool KeepGeometryInRAM)
{
	if (!std::filesystem::exists(Path))
	{
		return;
	}

	std::string String = Path.string();
	UINT64		Hash   = CityHash64(String.data(), String.size());
	if (MeshCache.Exist(Hash))
	{
		LOG_INFO("{} Exists", String);
		return;
	}

	Asset::MeshMetadata Metadata = { .Path = Path, .KeepGeometryInRAM = KeepGeometryInRAM };
	AsyncMeshLoader.RequestAsyncLoad(1, &Metadata);
}
