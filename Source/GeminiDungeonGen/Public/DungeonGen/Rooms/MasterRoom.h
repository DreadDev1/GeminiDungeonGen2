// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Data/Grid/GridData.h"
#include "Data/Room/RoomData.h"
#include "MasterRoom.generated.h"

UCLASS()
class GEMINIDUNGEONGEN_API AMasterRoom : public AActor
{
	GENERATED_BODY()

public:
	AMasterRoom();

	// --- Generation Parameters ---

	// The Data Asset defining this room's layout and content rules
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	URoomData* RoomDataAsset;

	// The seed used for generation (set by DungeonManager, tweakable by designer)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Generation")
	int32 GenerationSeed = 1337;

	// --- EDITOR ONLY: Generate Button ---
	// Changing this boolean property triggers the RegenerateRoom function in the editor.
	UPROPERTY(EditAnywhere, Category = "Generation|Debug")
	bool bGenerateRoom = false; 

	// --- Designer Override Control ---

	// Array of specific 100cm cell coordinates the designer wants to force empty.
	UPROPERTY(EditAnywhere, Category = "Designer Overrides|Floor")
	TArray<FIntPoint> ForcedEmptyFloorCells;
	
	// Array of specific meshes to force-place at coordinates (Hybrid System Control)
	UPROPERTY(EditAnywhere, Category = "Designer Overrides|Floor")
	TMap<FIntPoint, FMeshPlacementInfo> ForcedInteriorPlacements;

private:
	// Internal grid array to track occupancy (used during runtime generation)
	TArray<EGridCellType> InternalGridState;
	
	// Map to hold and manage HISM components (one HISM per unique Static Mesh)
	TMap<UStaticMesh*, UHierarchicalInstancedStaticMeshComponent*> MeshToHISMMap;
	
protected:
	virtual void PostLoad() override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// Override used to monitor changes in the Details Panel (for the bGenerateRoom button trick)
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	
	// --- Core Generation Functions ---

	// Selects one FMeshPlacementInfo struct based on placement weights
	const FMeshPlacementInfo* SelectWeightedMesh(const TArray<FMeshPlacementInfo>& MeshPool, FRandomStream& Stream);
	
	// UFUNCTION to be called by the DungeonManager (or designer in editor)
	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Generation")
	void RegenerateRoom();
	
	// Logic for clearing and resetting all HISM components
	void ClearAndResetComponents();
	
	// Logic for getting or creating the HISM component for a given mesh
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(UStaticMesh* Mesh);
	
	// Core grid packing logic (for floor and interior meshes)
	void GenerateFloorAndInterior();
	
	// 1D wall placement logic using WallDataAsset
	void GenerateWallsAndDoors();
	
	// Helper function for drawing the debug grid in the editor
	void DrawDebugGrid();
};

