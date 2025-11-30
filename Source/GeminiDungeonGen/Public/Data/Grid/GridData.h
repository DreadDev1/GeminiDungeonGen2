// GridData.h

#pragma once

#include "CoreMinimal.h"
#include "Engine/StaticMesh.h"
#include "GridData.generated.h"

// --- CORE CONSTANT DEFINITION ---
static constexpr float CELL_SIZE = 100.0f;

// Forward declaration for the MasterRoom to use this in USTRUCTs
class UWallDataAsset; 
class UFloorDataAsset;
class UDoorDataAsset;

// --- Enums ---

// Defines the content type of a 100cm grid cell
UENUM(BlueprintType)
enum class EGridCellType : uint8
{
	ECT_Empty 		UMETA(DisplayName = "Empty"),
	ECT_FloorMesh 	UMETA(DisplayName = "Floor Mesh"),
	ECT_Wall 		UMETA(DisplayName = "Wall Boundary"),
	ECT_Doorway 	UMETA(DisplayName = "Doorway Slot")
};

// --- Mesh Placement Info (Used by Floor and Interior Meshes) ---

// Struct for interior mesh definitions (e.g., clutter, furniture)
USTRUCT(BlueprintType)
struct FMeshPlacementInfo
{
	GENERATED_BODY()

	// The actual mesh asset to be placed. TSoftObjectPtr is good for Data Assets.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info")
	TSoftObjectPtr<UStaticMesh> MeshAsset; 

	// The size of the mesh footprint in 100cm cells (e.g., X=2, Y=4 for 200x400cm)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info")
	FIntPoint GridFootprint = FIntPoint(1, 1);

	// Relative weight for randomization (higher means more likely to be chosen)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info")
	float PlacementWeight = 1.0f;

	// If the mesh is non-square, define allowed rotations (e.g., 0 and 90)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Mesh Info")
	TArray<int32> AllowedRotations = {0}; 
};

// --- Wall Module Info ---

// Struct for complex wall modules (Base, Middle, Top)
USTRUCT(BlueprintType)
struct FWallModule
{
	GENERATED_BODY()

	// The length of this module in 100cm grid units (e.g., 2 for 200cm wall)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Info")
	int32 Y_AxisFootprint = 1;

	// Meshes that compose the module, using TSoftObjectPtr for async loading
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Meshes")
	TSoftObjectPtr<UStaticMesh> BaseMesh; 

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Meshes")
	TSoftObjectPtr<UStaticMesh> MiddleMesh;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Meshes")
	TSoftObjectPtr<UStaticMesh> TopMesh;
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Wall Info")
	float PlacementWeight = 1.0f; 
};