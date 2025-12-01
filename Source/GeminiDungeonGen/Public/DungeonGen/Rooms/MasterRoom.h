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

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Generation")
	URoomData* RoomDataAsset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Replicated, Category = "Generation|Seed")
	int32 GenerationSeed = 1337;

	// --- EDITOR ONLY: Generate Button ---
	UPROPERTY(EditAnywhere, Category = "Generation|Debug")
	bool bGenerateRoom = false; 

	// --- Designer Override Control ---

	// Array of specific 100cm cell coordinates the designer wants to force empty.
	UPROPERTY(EditAnywhere, Category = "Generation|Designer Overrides|Floor")
	TArray<FIntPoint> ForcedEmptyFloorCells;
	
	// Array of specific meshes to force-place at coordinates (Hybrid System Control)
	UPROPERTY(EditAnywhere, Category = "Generation|Designer Overrides|Floor")
	TMap<FIntPoint, FMeshPlacementInfo> ForcedInteriorPlacements;

private:
	// Internal grid array to track occupancy (used during runtime generation)
	TArray<EGridCellType> InternalGridState;
	
	// Map to hold and manage HISM components (one HISM per unique Static Mesh)
	TMap<UStaticMesh*, UHierarchicalInstancedStaticMeshComponent*> MeshToHISMMap;
	
protected:


#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
#endif
	
	// --- Core Generation Functions ---

	UFUNCTION(BlueprintCallable, CallInEditor, Category = "Generation")
	void RegenerateRoom();

	// Logic for clearing and resetting all HISM components
	void ClearAndResetComponents();
	UHierarchicalInstancedStaticMeshComponent* GetOrCreateHISM(UStaticMesh* Mesh);

	// New helper for easy world coordinate translation
	FVector GetCellCenterWorldLocation(int32 X, int32 Y) const;

	// Floor and Interior Logic
	void ExecuteForcedPlacements(FRandomStream& Stream);
	void GenerateFloorAndInterior();

	// 1D wall placement logic using WallDataAsset
	void GenerateWallsAndDoors();
	
	const FMeshPlacementInfo* SelectWeightedMesh(const TArray<FMeshPlacementInfo>& MeshPool, FRandomStream& Stream);
	
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;
	
	// Helper function for drawing the debug grid in the editor
	void DrawDebugGrid();
};

