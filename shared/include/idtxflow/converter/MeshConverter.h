#pragma once

/**
 * @file MeshConverter.h
 * @brief Converter for UsdGeomMesh prims into a generic MeshDescription format that can be further processed into
 * engine-specific mesh assets.
 * 
 * The conversion extracts vertex positions, face indices, normals, UVs, vertex colors and material assignments.
 * It does not support more complex features like blend shapes or skeletal animation.
 * 
 * Skeletal Prims are handled separately in the UsdSkeletalMeshConverter, which extracts the skinning data and skeleton bindings.
 * The actual mesh data is extracted in this converter and can be used by the skeletal mesh converter to create the final skeletal mesh asset.
 */
#include <algorithm>
#include <array>
#include <map>
#include <optional>
#include <type_traits>
#include <vector>

#include <pxr/base/tf/token.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdSkel/animQuery.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/blendShape.h>
#include <pxr/usd/usdSkel/skeletonQuery.h>
#include <pxr/usd/usdSkel/skinningQuery.h>

#include "../types/TargetTypes.h"
#include "TypeConverter.h"

namespace idtxflow
{
namespace converter
{
	template<typename MeshDataType>
	struct MeshDescription
	{
		MeshDataType meshData;
		pxr::UsdShadeMaterial usdMaterial;
		// USD authors doubleSided per-mesh (UsdGeomGprim); engines key it
		// per-material, so the caller propagates this onto the bound material.
		bool doubleSided = false;
	};
	
	template<typename TargetEngine> requires idtxflow::types::ValidTargetEngine<TargetEngine>
	class UsdMeshConverter
	{
	public:
		using Types = idtxflow::types::TargetEngineTypes<TargetEngine>;
		using TypeConverter = UsdTypeConverter<TargetEngine>;
		using MeshBuilder = TargetMeshBuilder<TargetEngine>;
		using MeshDataType = typename Types::MeshData;

		/**
		 * General core conversation of an UsdGeomMesh prim into the data structures required by the
		 * respective target engine, the usd data should be converted into. The resulting array consists of
		 * self-contained complete mesh definitions with its points, normal vectors, uv and vertex colors if authored.
		 * It's the responsibility of the calling code, implementetd for the specific engine, using the returned data
		 * and adds it to the respective objects, like StaticMeshComponents or similar.
		 * @param usdMesh The reference to the usd prim.
		 * @return Array of self-contained mesh data container
		 */
		std::vector<MeshDescription<MeshDataType>> Convert(const pxr::UsdGeomMesh& usdMesh)
		{
			if (!usdMesh.GetPointsAttr().Get(&points))
				return {}; // a mesh w/o points does not make any sense
			if (!usdMesh.GetFaceVertexCountsAttr().Get(&facePointCounts))
				return {}; // a mesh w/o faces does not make any sense
			if (!usdMesh.GetFaceVertexIndicesAttr().Get(&facePointIndices))
				return {}; // a mesh w/o face-vertex-index assignment does not make any sense

			// calculate the offsets into the facePointIndices list from the facePointCounts list
			// TODO: we might be able to skip this, if we can add some custom metadata to the mesh
			// if it has been pre-optimized to always contain faces with the same amount of points
			int offset = 0;
			facePointOffsets.reserve(facePointCounts.size());
			for (int& faceCount: facePointCounts)
			{
				facePointOffsets.push_back(offset);
				offset += faceCount;
			}

			if (!usdMesh.GetOrientationAttr().Get(&orientation))
				orientation = pxr::UsdGeomTokens->leftHanded; // default the face orientation to leftHanded
			
			usdMesh.GetDisplayColorAttr().Get(&colors);
			colorInterpolation = usdMesh.GetDisplayColorPrimvar().GetInterpolation();

			usdMesh.GetNormalsAttr().Get(&normals);
			normalInterpolation = usdMesh.GetNormalsInterpolation();

			// extracting the texture coordinates (UV-Map) for a mesh from USD could be a bit tricky.
			// the texture coordinate list is authored as PrimVar to the mesh. However, even though the
			// name is quite standard, the actual attribute used is quite often authored in the assigned shader/material
			// with a specific variable in the "inputs" namespace. Thus we would require to lookup the assigned shader here.
			// To reduce this complexity we stick to the common naming convention to have the UV's authored as "st" or "st0".
			if (const pxr::UsdGeomPrimvarsAPI& primVars = pxr::UsdGeomPrimvarsAPI(usdMesh.GetPrim()))
			{
				pxr::UsdGeomPrimvar texcoords_var;
				if (primVars.HasPrimvar(pxr::TfToken("st")))
					texcoords_var = primVars.GetPrimvar(pxr::TfToken("st"));
				else if (primVars.HasPrimvar(pxr::TfToken("st0")))
					texcoords_var = primVars.GetPrimvar(pxr::TfToken("st0"));

				if (texcoords_var)
				{
					texcoords_var.Get(&texcoords);
					texcoords_var.GetIndices(&texcoordIndices);
					texcoordsInterpolation = texcoords_var.GetInterpolation();
				}
			}

			// doubleSided is a per-mesh (UsdGeomGprim) attribute; capture it once
			// here and stamp it on every MeshDescription so the caller can fold it
			// into the per-material flag the engines actually use.
			bool meshDoubleSided = false;
			usdMesh.GetDoubleSidedAttr().Get(&meshDoubleSided);

			// once we have extracted all relevant data from the UsdGeomMeshPrim we can run the conversion into the
			// target engine format
			std::vector<MeshDescription<MeshDataType>> engineMesh;

			// as the USD mesh may contain subsets we need to create the subset meshes first and finish of with the remainder.
			// while the USD subsets does only authore an index list of faces/edges/points into that refer to the original mesh
			// we need to create self-containing complete meshes for the target engine containing all vertices and face indicies.
			// we will store all subset faces that has been considered already. Only those faces that have not been considered at all
			// will then be converted in a final step. If a mesh does not contain any subsets, this will lead to converting the
			// whole mesh in the final step, as expected.
			pxr::VtArray<int> handledSubsetFaces;
			for (const pxr::UsdGeomSubset& subset : pxr::UsdGeomSubset::GetAllGeomSubsets(usdMesh))
			{
				class pxr::TfToken subsetType;
				// if the subset does not define it's type (face, edge, point) we can't handle it
				if (!subset.GetElementTypeAttr().Get(&subsetType)) continue;

				// for the time beeing we only handle subsets of type "Face".
				if (subsetType != pxr::UsdGeomTokens->face) continue;

				pxr::VtArray<int> subsetFaces;
				// if the subset does not define any faces we can't handle it
				if (!subset.GetIndicesAttr().Get(&subsetFaces)) continue;

				// now build the mesh data for this subset
				MeshDescription<MeshDataType> subsetData;
				subsetData.meshData = BuildMesh(subsetFaces);

				// store the handled faces in the respective list
				pxr::VtArray<int> tmpSubsetFaces;
				tmpSubsetFaces.resize(handledSubsetFaces.size() + subsetFaces.size());
				std::copy(handledSubsetFaces.begin(), handledSubsetFaces.end(), tmpSubsetFaces.begin());
				std::copy(subsetFaces.begin(), subsetFaces.end(), tmpSubsetFaces.begin() + handledSubsetFaces.size());
				handledSubsetFaces = tmpSubsetFaces;
				
				// TODO: get the material assigned to this subset if any and either convert here, or
				// let the caller do the convertion and further processing of the material and just
				// provide the material reference
				subsetData.usdMaterial = GetUsdMaterial(subset);
				subsetData.doubleSided = meshDoubleSided;

				// push the subset mesh to the mesh data list. It's up to the engine to process the different meshes and create
				// the respective data in the engine format
				engineMesh.push_back(subsetData);
			}

			// after handling all subsets - if any - handle the remainder of the mesh
			pxr::VtArray<int> remainingFaces;
			if (!handledSubsetFaces.empty() && handledSubsetFaces.size() < facePointCounts.size())
			{
				// calculate the remaining faces
				remainingFaces.reserve(facePointCounts.size() - handledSubsetFaces.size());
				for (int f = 0; f < facePointCounts.size(); ++f)
				{
					// if the face is not found in the subset faces, we need to consider it for conversion
					if (std::find(handledSubsetFaces.begin(), handledSubsetFaces.end(), f) == handledSubsetFaces.end())
						remainingFaces.push_back(f);
				}
			}

			// if either remaining faces exists, or no subset has been handled, convert the remaining faces of
			// the mesh. if remainingFaces is empty, the whole mesh is converted.
			if (!remainingFaces.empty() || handledSubsetFaces.empty())
			{
				MeshDescription<MeshDataType> meshData;
				meshData.meshData = BuildMesh(remainingFaces);

				// TODO: get the material assigned to this subset if any and either convert here, or
				// let the caller do the convertion and further processing of the material and just
				// provide the material reference
				meshData.usdMaterial = GetUsdMaterial(usdMesh);
				meshData.doubleSided = meshDoubleSided;

				// push the mesh data to the list. It's up to the engine to process the different meshes and create
				// the respective data in the engine format
				engineMesh.push_back(meshData);
			}

			return engineMesh;
		}

	    std::vector<MeshDescription<MeshDataType>> ConvertSkinnedMesh(
	        const pxr::UsdSkelSkinningQuery& skinningQuery,
	        const pxr::UsdSkelSkeletonQuery& skelQuery,
	        const std::map<std::string, int32_t>& boneNameToIndexMap)
		{
		    pxr::UsdGeomMesh usdMesh(skinningQuery.GetPrim());
		    if (!usdMesh) return {};

		    skinningQuery.GetJointOrder(&joint_order);
		    skinningQuery.GetJointIndicesPrimvar().Get(&joint_indices);
		    joint_index_element_size = skinningQuery.GetJointIndicesPrimvar().GetElementSize();
		    skinningQuery.GetJointWeightsPrimvar().Get(&joint_weights);
		    joint_weights_element_size = skinningQuery.GetJointWeightsPrimvar().GetElementSize();

		    boneNameToIndex = boneNameToIndexMap;

		    // Blend shapes: read each UsdSkelBlendShape target, densify its sparse
		    // offsets to one entry per USD point, and resolve its current weight from
		    // the bound animation. BuildMesh maps these onto the indexed vertices.
		    blend_shapes.clear();
		    {
		        pxr::UsdSkelBindingAPI bsBinding(skinningQuery.GetPrim());
		        pxr::VtArray<pxr::TfToken> bsNames;
		        pxr::SdfPathVector bsTargets;
		        if (bsBinding.GetBlendShapesAttr().Get(&bsNames)
		            && bsBinding.GetBlendShapeTargetsRel().GetTargets(&bsTargets))
		        {
		            pxr::VtArray<pxr::GfVec3f> pts;
		            usdMesh.GetPointsAttr().Get(&pts);
		            const size_t numPoints = pts.size();

		            // Current weight per blend-shape name, from the skeleton's animation.
		            std::map<std::string, float> weightByName;
		            if (pxr::UsdSkelAnimQuery animQuery = skelQuery.GetAnimQuery())
		            {
		                pxr::VtArray<pxr::TfToken> animOrder = animQuery.GetBlendShapeOrder();
		                pxr::VtArray<float> animWeights;
		                animQuery.ComputeBlendShapeWeights(&animWeights);
		                for (size_t i = 0; i < animOrder.size() && i < animWeights.size(); ++i)
		                {
		                    weightByName[animOrder[i].GetString()] = animWeights[i];
		                }
		            }

		            pxr::UsdStagePtr stage = skinningQuery.GetPrim().GetStage();
		            const size_t count = std::min(bsNames.size(), bsTargets.size());
		            for (size_t b = 0; b < count; ++b)
		            {
		                pxr::UsdSkelBlendShape bs(stage->GetPrimAtPath(bsTargets[b]));
		                if (!bs) continue;
		                BlendShapeSrc src;
		                src.name = bsNames[b].GetString();
		                std::map<std::string, float>::const_iterator wit = weightByName.find(src.name);
		                src.weight = (wit != weightByName.end()) ? wit->second : 0.0f;

		                pxr::VtArray<pxr::GfVec3f> offsets;
		                pxr::VtArray<int> pointIndices;
		                pxr::VtArray<pxr::GfVec3f> normalOffsets;
		                bs.GetOffsetsAttr().Get(&offsets);
		                bs.GetPointIndicesAttr().Get(&pointIndices);
		                bs.GetNormalOffsetsAttr().Get(&normalOffsets);
		                src.has_normals = !normalOffsets.empty();
		                src.pos.assign(numPoints, pxr::GfVec3f(0.0f));
		                if (src.has_normals)
		                {
		                    src.nrm.assign(numPoints, pxr::GfVec3f(0.0f));
		                }
		                if (pointIndices.empty())
		                {
		                    for (size_t i = 0; i < offsets.size() && i < numPoints; ++i)
		                    {
		                        src.pos[i] = offsets[i];
		                    }
		                    if (src.has_normals)
		                    {
		                        for (size_t i = 0; i < normalOffsets.size() && i < numPoints; ++i)
		                        {
		                            src.nrm[i] = normalOffsets[i];
		                        }
		                    }
		                }
		                else
		                {
		                    for (size_t k = 0; k < pointIndices.size() && k < offsets.size(); ++k)
		                    {
		                        int pi = pointIndices[k];
		                        if (pi >= 0 && static_cast<size_t>(pi) < numPoints) { src.pos[pi] = offsets[k]; }
		                    }
		                    if (src.has_normals)
		                    {
		                        for (size_t k = 0; k < pointIndices.size() && k < normalOffsets.size(); ++k)
		                        {
		                            int pi = pointIndices[k];
		                            if (pi >= 0 && static_cast<size_t>(pi) < numPoints) { src.nrm[pi] = normalOffsets[k]; }
		                        }
		                    }
		                }
		                blend_shapes.push_back(std::move(src));
		            }
		        }
		    }

		    return Convert(usdMesh);
		}

	    pxr::UsdShadeMaterial GetUsdMaterial(const pxr::UsdSchemaBase& schemaObj)
		{
			pxr::UsdShadeMaterialBindingAPI material_binding(schemaObj);
		    // MaterialBindingAPI is a "builtIn" schema and should always be available for Geom prims. But to be save
		    // we fall back to a default material in this case
		    if (!material_binding) return pxr::UsdShadeMaterial();
		    
		    pxr::UsdRelationship binding_relationship;
		    pxr::UsdShadeMaterial material = material_binding.ComputeBoundMaterial(pxr::UsdShadeTokens->allPurpose, &binding_relationship);
		    if (material) return material;
		    // if the BindingRelation ship is invalid it means, that no material binding has been authored. This is ok and will
		    // lead to a default material beeing used.
		    if (!binding_relationship.IsValid()) return pxr::UsdShadeMaterial();
		    
		    // not being able to get the material binding could be, that the bound material is part of another usd layer that is
		    // referenced via payload and the payload is not yet loaded. Check for this case. As this may be the case along the
		    // path to the actual material on any parent level we walk the path up.
		    // even though there could be technically multiple targets maintained for the material binding relationship
		    // we will only consider the first one.
			pxr::UsdStageRefPtr stage = schemaObj.GetPrim().GetStage();
		    pxr::SdfPathVector targets;
		    binding_relationship.GetTargets(&targets);
		    for (const pxr::SdfPath& target : targets)
		    {
		        pxr::UsdPrim material_prim = stage->GetPrimAtPath(target);
		        while (!material_prim)
		        {
		            pxr::SdfPath parent = target.GetParentPath();
		            if (parent.IsEmpty() || parent.IsAbsoluteRootPath()) break;
		            material_prim = stage->GetPrimAtPath(parent);
		        }

		        if (material_prim)
		        {
		            // now ensure the prim is loaded, this will also load any descendant prims. We assume, this load is not heavy
		            // and will occur for the "materials in payload usd layers" use case
		            material_prim.Load();
		            material = material_binding.ComputeBoundMaterial(pxr::UsdShadeTokens->allPurpose);
		            // if this still would not provide a material, we will fallback to a default one, which is fine then
		            // for the time being.
		            return material;
		        }

		        break;
		    }
			
		    return pxr::UsdShadeMaterial();
		}

	protected:
		// build the target engines mesh data. If the usdMesh has been split into subsets, the faceFilter provides the
		// faces of the subset that shall be considered for mesh creation.
		MeshDataType BuildMesh(const pxr::VtArray<int>& faceFilter)
		{
			MeshDataType meshData;
			MeshBuilder builder;
			
			source_points.clear();
			// One engine vertex per (USD point, normal source, uv source). Vertex /
			// constant-interp attributes share by point so the mesh stays indexed
			// (1:1 with USD points); only faceVarying / per-face seams split a point.
			std::map<std::array<int32_t, 3>, typename Types::Index> vertex_lookup;

			// run the conversion face by face. Either all faces of the mesh or only the ones provided in the filer
			int faces = !faceFilter.empty() ? faceFilter.size() : facePointCounts.size();
			for (int f = 0; f < faces; ++f)
			{
				// get the face index
				int faceIndex = !faceFilter.empty() ? faceFilter[f] : f;
				// get the point count of this face and the offset into the point index list
				int pointCount = facePointCounts[faceIndex];
				int facePointOffset = facePointOffsets[faceIndex];

				if (pointCount < 3) continue; // we will not convert degenerate faces

				// while USD most likely stores mesh data in an optimized format and re-uses vertex data as much as
				// as possible, game engine usually require all data (point, normal, uv, color) to be stored in arrays
				// of the same size and thus for each individual point/vertex. Thus sharing data of a vertex for
				// multiple faces would only be possible if all faces, using the same point would also have the same
				// uv, normal and color authored for all faces sharing this point.
				// To reduce complexity in the initial conversion version we assume that each point has to have it's unique
				// data and thus each point and it's related data is copied for each face using this point. It's a known
				// issue, that this can lead to a much higher memory consumption then necessary and may also have negative
				// impact on rendering performance.
				// This face's deduped engine-vertex indices, in winding order.
				std::vector<typename Types::Index> faceVerts;
				faceVerts.reserve(pointCount);
				for (int p = 0; p < pointCount; ++p)
				{
					int pointIndex = facePointIndices[facePointOffset + p];
					typename Types::Vector3 point = TypeConverter::toVector3(points[pointIndex]);
					typename Types::Vector3 normal;
					typename Types::Vector2 uv;

					// if the mesh provided normals we need to convert them, too
					if (!normals.empty())
					{
						// how the normals are converted depends on their interpolation type
						if (normalInterpolation == pxr::UsdGeomTokens->vertex)
						{
							// normals are authored for each point
							normal = TypeConverter::toVector3(normals[pointIndex]);
						} else if (normalInterpolation == pxr::UsdGeomTokens->faceVarying)
						{
							// normals are authored for each point of a face
							normal = TypeConverter::toVector3(normals[facePointOffset + p]);
						} else if (normalInterpolation == pxr::UsdGeomTokens->uniform)
						{
							// normals are authored for each face only
							normal = TypeConverter::toVector3(normals[faceIndex]);
						}
					} else
					{
						// we need to calculate a normal vector for this point if none are provided in the
						// usdMesh. Omitting normal vectors completely in game engines can lead to rndering
						// artifacts.
						// to keep normal calculation simple we calculate the normal of the point, based on the
						// face it belongs to
						int vidx1 = facePointIndices[facePointOffset + 0];
						int vidx2 = facePointIndices[facePointOffset + 1];
						int vidx3 = facePointIndices[facePointOffset + 2];
						class pxr::GfVec3d edge1 = points[vidx2] - points[vidx1];
						class pxr::GfVec3d edge2 = points[vidx3] - points[vidx1];
						normal = TypeConverter::toVector3(pxr::GfCross(edge1, edge2).GetNormalized());
					}

					// if the mesh provided texture coordinates (uv mapping) convert them, too
					if (!texcoords.empty())
					{
						// texture coordinats need to be handled based on interpolation type
						if (texcoordsInterpolation == pxr::UsdGeomTokens->vertex)
						{
							// uv's are authored for each point
							uv = TypeConverter::toVector2(texcoords[pointIndex]);
						} else if (texcoordsInterpolation == pxr::UsdGeomTokens->faceVarying)
						{
							// uv's are authored for each point per face. In this case the uv's might be
							// shared and thus referenced using the texcoordIndex list
							int uvIndex = texcoordIndices.empty() ? facePointOffset + p : texcoordIndices[facePointOffset + p];
							uv = TypeConverter::toVector2(texcoords[uvIndex]);
						}

						// flip y value
						// TODO: is there a reliable way to know when to flip and when not? Actually it seems always to be required
					    uv = FlipUvV(uv);
					}

				    // if the mesh provided skinning data we will convert them to
				    std::vector<uint32_t> boneIdx;
				    std::vector<float> boneWeight;
				    if (!joint_indices.empty())
				    {
				        // Target engines cap influences at 4 per vertex; ignore any extra.
				        int num_elements = std::min(joint_index_element_size, 4);
				        for (int element=0; element < num_elements; ++element)
				        {
				            // Index indices and weights with THEIR OWN element sizes, and
				            // bounds-check both: malformed/short primvars (or a weights
				            // stride != indices stride) must never read past either array.
				            const size_t ji = static_cast<size_t>(pointIndex) * joint_index_element_size + element;
				            const size_t wi = static_cast<size_t>(pointIndex) * joint_weights_element_size + element;
				            if (ji >= joint_indices.size() || wi >= joint_weights.size())
				            {
				                break;
				            }

				            const int joint_idx = joint_indices[ji];
				            int bone_idx = 0;
				            bool resolved = false;
				            if (joint_order.empty())
				            {
				                // No per-mesh joint order: the indices already address the
				                // global skeleton joint list as-is.
				                if (joint_idx >= 0)
				                {
				                    bone_idx = joint_idx;
				                    resolved = true;
				                }
				            }
				            else if (joint_idx >= 0 && joint_idx < static_cast<int>(joint_order.size()))
				            {
				                // Per-mesh subset: indices are LOCAL to joint_order. Map the
				                // joint NAME to its global skeleton bone index via find()
				                // (never operator[] — an unknown name must not insert a 0).
				                std::map<std::string, int32_t>::const_iterator it =
				                    boneNameToIndex.find(joint_order[joint_idx].GetString());
				                if (it != boneNameToIndex.end())
				                {
				                    bone_idx = it->second;
				                    resolved = true;
				                }
				            }
				            // Unresolved influences bind to bone 0 with zero weight so they
				            // cannot deform the mesh (avoids the "spikes from origin" artefact).
				            boneIdx.push_back(static_cast<uint32_t>(bone_idx));
				            boneWeight.push_back(resolved ? joint_weights[wi] : 0.0f);
				        }
				    }

					// Source-index of each face-varying attribute (vertex / constant
					// share by point; faceVarying by its own index; uniform or computed
					// normals are per-face -> negative id so they never alias a point).
					int32_t nrmVid = pointIndex;
					if (normals.empty())
					{
						nrmVid = -1 - faceIndex;
					}
					else if (normalInterpolation == pxr::UsdGeomTokens->faceVarying)
					{
						nrmVid = facePointOffset + p;
					}
					else if (normalInterpolation == pxr::UsdGeomTokens->uniform)
					{
						nrmVid = -1 - faceIndex;
					}
					int32_t uvVid = pointIndex;
					if (!texcoords.empty() && texcoordsInterpolation == pxr::UsdGeomTokens->faceVarying)
					{
						uvVid = texcoordIndices.empty() ? (facePointOffset + p)
						                                : texcoordIndices[facePointOffset + p];
					}
					else if (!texcoords.empty() && texcoordsInterpolation == pxr::UsdGeomTokens->uniform)
					{
						uvVid = -1 - faceIndex;
					}

					std::array<int32_t, 3> vkey = { pointIndex, nrmVid, uvVid };
					typename std::map<std::array<int32_t, 3>, typename Types::Index>::iterator found = vertex_lookup.find(vkey);
					typename Types::Index vi;
					if (found != vertex_lookup.end())
					{
						vi = found->second;
					}
					else
					{
						vi = builder.GetVertexCount(meshData);
						builder.AddVertex(meshData, point, normal, uv, boneIdx, boneWeight);
						source_points.push_back(pointIndex);
						vertex_lookup.emplace(vkey, vi);
					}
					faceVerts.push_back(vi);
				}

				// Triangulate this face over its deduped vertices, PRESERVING the
				// source winding (fan: 0, i, i+1). The up-axis change of basis is a
				// proper rotation (det +1), so it does not flip winding -- emitting
				// the reversed fan (0, i+1, i) made every mesh wind opposite to its
				// authored normals + `orientation`, rendering inside-out under
				// back-face culling (Godot CULL_BACK, three.js front side, re-import).
				for (int i = 1; i + 1 < pointCount; ++i)
				{
					builder.AddIndex(meshData, faceVerts[0]);
					builder.AddIndex(meshData, faceVerts[i]);
					builder.AddIndex(meshData, faceVerts[i + 1]);
				}
			}

			// Map each blend shape's per-point deltas onto the indexed engine
			// vertices (source_points[v] is v's USD point). Sparse output: only
			// vertices the shape actually moves. Compiled only for targets whose
			// MeshData carries a blend-shape list (FlatTree); a no-op otherwise.
			if constexpr (requires (MeshDataType m) { m.BlendShapes; })
			{
				for (const BlendShapeSrc& bs : blend_shapes)
				{
					typename std::decay<decltype(meshData.BlendShapes)>::type::value_type fbs;
					fbs.name = bs.name;
					fbs.weight = bs.weight;
					fbs.has_normals = bs.has_normals;
					for (size_t v = 0; v < source_points.size(); ++v)
					{
						const pxr::GfVec3f& d = bs.pos[source_points[v]];
						bool moved = (d[0] != 0.0f || d[1] != 0.0f || d[2] != 0.0f);
						if (bs.has_normals && !moved)
						{
							const pxr::GfVec3f& nd = bs.nrm[source_points[v]];
							moved = (nd[0] != 0.0f || nd[1] != 0.0f || nd[2] != 0.0f);
						}
						if (!moved) { continue; }
						fbs.indices.push_back(static_cast<int32_t>(v));
						fbs.pos_offsets.push_back(TypeConverter::toVector3(d));
						if (bs.has_normals)
						{
							fbs.nrm_offsets.push_back(TypeConverter::toVector3(bs.nrm[source_points[v]]));
						}
					}
					// Keep zero-delta targets too (e.g. VRChat's vrc.v_sil / vrc.v_pp):
					// the NAME must survive the round-trip even when no vertex moves,
					// or viseme / expression sets break downstream. Empty indices just
					// means the host registers an all-zero morph.
					meshData.BlendShapes.push_back(std::move(fbs));
				}
			}

			return meshData;
		}

		// as we don't know the components of the used UV representations we need to specialize this
	    // one for each target engine
	    static typename Types::Vector2 FlipUvV(const typename Types::Vector2& input);
	    
	private:
		// Per output vertex, the source USD point index it was de-indexed from.
		// Populated by BuildMesh so morph-target offsets (authored per USD point)
		// map onto the indexed engine vertices without a separate scatter table.
		std::vector<int32_t> source_points;
		// Blend-shape (morph target) source data, read from the mesh's
		// UsdSkelBlendShape targets and densified per USD point (zero where the
		// shape leaves a point untouched). Weight comes from the bound animation.
		// Populated by ConvertSkinnedMesh; consumed in BuildMesh via source_points.
		struct BlendShapeSrc {
			std::string               name;
			float                     weight = 0.0f;
			bool                      has_normals = false;
			std::vector<pxr::GfVec3f> pos;   // size == points.size()
			std::vector<pxr::GfVec3f> nrm;   // size == points.size() (if has_normals)
		};
		std::vector<BlendShapeSrc> blend_shapes;
		// the points of the mesh, as defined in the usd prim
		pxr::VtArray<class pxr::GfVec3f> points;
		// the list of number of points that span the face at index
		pxr::VtArray<int> facePointCounts;
		// list of indices for each point of a face into points
		pxr::VtArray<int> facePointIndices;
		// orientation of faces (order of points) in the mesh (Right- or LeftHanded)
		class pxr::TfToken orientation;
		// as the number of points per face may vary, we store the offset into facePointIndices for
		// fast and convinient access to the correct point index for a specif face
		pxr::VtArray<int> facePointOffsets;
		// list of colors of the mesh. Number of entries varies based on interpolation type.
		// Interpolation could be Face, Point or Constant
		pxr::VtArray<class pxr::GfVec3f> colors;
		class pxr::TfToken colorInterpolation;
		// list of normal vectors. Number of entries varies based on interpolation type.
		// Interpolation could be Face, Edge, Point
		pxr::VtArray<class pxr::GfVec3f> normals;
		class pxr::TfToken normalInterpolation;
		// list of texture coordinates (UV mappings) of the points.
		// Number of entries matches points if no texcoords_indices are provided
		pxr::VtArray<class pxr::GfVec2f> texcoords;
		// list if indices into the texcoords array. If this list is provided the number of entries
		// should match the number of points
		pxr::VtArray<int> texcoordIndices;
		class pxr::TfToken texcoordsInterpolation;

	    // to allow skinning of a skeleton based on this mesh we use the joints (bones) and their attributes
	    pxr::VtArray<class pxr::TfToken> joint_order;
	    pxr::VtArray<int> joint_indices;
	    int joint_index_element_size = 0;
	    pxr::VtArray<float> joint_weights;
	    int joint_weights_element_size = 0;

	    std::map<std::string, int32_t> boneNameToIndex;
	};
}
}