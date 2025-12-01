// Fill out your copyright notice in the Description page of Project Settings.


#include "DungeonGen/Rooms/MasterRoom.h"
#include "Net/UnrealNetwork.h"
#include "DrawDebugHelpers.h" // Needed for debug drawing
#include "Data/Room/FloorData.h"


// Sets default values
AMasterRoom::AMasterRoom()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true; // Essential for multiplayer
	// Ensure the root component is set up for transforms
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void AMasterRoom::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMasterRoom, GenerationSeed);
}

// --- Editor Debug/Button Logic ---

void AMasterRoom::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	// Check if the property that changed was 'bGenerateRoom'
	FName PropertyName = (PropertyChangedEvent.Property != nullptr) ? PropertyChangedEvent.Property->GetFName() : NAME_None;

	if (PropertyName == GET_MEMBER_NAME_CHECKED(AMasterRoom, bGenerateRoom))
	{
		if (bGenerateRoom)
		{
			RegenerateRoom();
			bGenerateRoom = false; // Reset the button immediately after execution
		}
	}
	
	// IMPORTANT: Call the debug drawing here so it updates instantly in the editor
	if (GIsEditor)
	{
		DrawDebugGrid();
	}
}

// --- Helper: Weighted Random Selection ---

// Selects one FMeshPlacementInfo struct based on placement weights

const FMeshPlacementInfo* AMasterRoom::SelectWeightedMesh(const TArray<FMeshPlacementInfo>& MeshPool, FRandomStream& Stream)
{
	if (MeshPool.Num() == 0)
	{
		return nullptr;
	}

	// Calculate total weight
	float TotalWeight = 0.0f;
	for (const FMeshPlacementInfo& Info : MeshPool)
	{
		TotalWeight += Info.PlacementWeight;
	}

	if (TotalWeight <= 0.0f)
	{
		return &MeshPool[Stream.RandRange(0, MeshPool.Num() - 1)]; // Fallback to uniform random
	}

	// Choose a random point in the total weight range
	float RandomWeight = Stream.FRand() * TotalWeight;
	
	// Find which mesh corresponds to that weight point
	float CurrentWeight = 0.0f;
	for (const FMeshPlacementInfo& Info : MeshPool)
	{
		CurrentWeight += Info.PlacementWeight;
		if (RandomWeight <= CurrentWeight)
		{
			return &Info;
		}
	}

	return &MeshPool.Last(); // Should not be reached, but safe fallback
}

void AMasterRoom::RegenerateRoom()
{
	// Server Check: Only the server or the editor should run generation
	if (GetLocalRole() != ROLE_Authority && !IsEditorOnly() && !GIsEditor)
	{
		return;
	}

	if (!RoomDataAsset)
	{
		UE_LOG(LogTemp, Warning, TEXT("ADungeonMasterRoom: RoomDataAsset is null. Cannot generate."));
		return;
	}
	
	// 1. Clean up and prepare for a new generation pass
	ClearAndResetComponents();

	// 2. Run generation steps
	GenerateFloorAndInterior();
	GenerateWallsAndDoors();
	
	// 3. Force bounding box updates on all new and existing components
	for (const auto& Pair : MeshToHISMMap)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Value)
		{
			// Forces the component to re-evaluate its spatial boundaries based on new instances
			HISM->UpdateBounds(); 
			HISM->MarkRenderStateDirty(); // Ensures the rendering thread picks up the change
		}
	}

	// In Editor, this is the most reliable way to force a complete bounds update on the actor
#if WITH_EDITOR
	RerunConstructionScripts();
#endif
	
	// 4. Update the debug visuals immediately
	if (GIsEditor)
	{
		DrawDebugGrid();
	}
}

void AMasterRoom::DrawDebugGrid()
{
	if (!RoomDataAsset) return;

	const FIntPoint GridSize = RoomDataAsset->GridSize;
	const FVector ActorLocation = GetActorLocation();
	const UWorld* World = GetWorld();
	if (!World) return;

	// 1. Draw Grid Lines (Green)
	
	// Draw X-Axis lines
	for (int32 X = 0; X <= GridSize.X; ++X)
	{
		FVector Start = ActorLocation + FVector(X * CELL_SIZE, 0.0f, 0.0f);
		FVector End = ActorLocation + FVector(X * CELL_SIZE, GridSize.Y * CELL_SIZE, 0.0f);
		DrawDebugLine(World, Start, End, FColor::Green, false, 5.0f, 0, 5.0f);
	}

	// Draw Y-Axis lines
	for (int32 Y = 0; Y <= GridSize.Y; ++Y)
	{
		FVector Start = ActorLocation + FVector(0.0f, Y * CELL_SIZE, 0.0f);
		FVector End = ActorLocation + FVector(GridSize.X * CELL_SIZE, Y * CELL_SIZE, 0.0f);
		DrawDebugLine(World, Start, End, FColor::Green, false, 5.0f, 0, 5.0f);
	}
	
	// 2. Draw Cell State Boxes (Red/Blue)
	
	for (int32 Y = 0; Y < GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < GridSize.X; ++X)
		{
			int32 Index = Y * GridSize.X + X;
			if (InternalGridState.IsValidIndex(Index))
			{
				// Center of the cell
				FVector Center = ActorLocation + FVector(
					(X + 0.5f) * CELL_SIZE, 
					(Y + 0.5f) * CELL_SIZE, 
					20.0f // Lift the box slightly above Z=0
				);
				
				// Size of the box (half extent)
				FVector Extent(CELL_SIZE / 2.0f, CELL_SIZE / 2.0f, 20.0f);
				
				FColor BoxColor = (InternalGridState[Index] != EGridCellType::ECT_Empty) ? FColor::Red : FColor::Blue;

				DrawDebugBox(World, Center, Extent, FQuat::Identity, BoxColor, false, 5.0f, 0, 3.0f);
			}
		}
	}
}

// --- Component Management ---

void AMasterRoom::ClearAndResetComponents()
{
	// 1. Clear all instances from existing HISM components
	for (const auto& Pair : MeshToHISMMap)
	{
		if (UHierarchicalInstancedStaticMeshComponent* HISM = Pair.Value)
		{
			HISM->ClearInstances();
		}
	}
	
	// 2. Reset internal grid state
	InternalGridState.Empty();
	if (RoomDataAsset)
	{
		int32 TotalCells = RoomDataAsset->GridSize.X * RoomDataAsset->GridSize.Y;
		// Initialize all cells as empty before generation starts
		InternalGridState.Init(EGridCellType::ECT_Empty, TotalCells); 
	}
}

UHierarchicalInstancedStaticMeshComponent* AMasterRoom::GetOrCreateHISM(UStaticMesh* Mesh)
{
	if (!Mesh) return nullptr;

	// Use a raw pointer for the key since UStaticMesh is a UObject and handles its own lifecycle
	if (UHierarchicalInstancedStaticMeshComponent** HISM_Ptr = MeshToHISMMap.Find(Mesh))
	{
		return *HISM_Ptr;
	}
	else
	{
		// Create a new HISM component for this unique mesh
		FString ComponentName = FString::Printf(TEXT("HISM_%s"), *Mesh->GetName());
		UHierarchicalInstancedStaticMeshComponent* NewHISM = NewObject<UHierarchicalInstancedStaticMeshComponent>(this, FName(*ComponentName));
		
		if (NewHISM)
		{
			NewHISM->SetStaticMesh(Mesh);
			NewHISM->RegisterComponent();
			NewHISM->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
			
			MeshToHISMMap.Add(Mesh, NewHISM);
			return NewHISM;
		}
	}
	return nullptr;
}

// --- Generation Implementation Sketch ---

void AMasterRoom::GenerateFloorAndInterior()
{
	if (!RoomDataAsset) return;

	// Use a seeded random stream for predictable generation
	FRandomStream RandomStream(GenerationSeed);
	
	const FIntPoint GridSize = RoomDataAsset->GridSize;
	const UFloorData* FloorData = RoomDataAsset->FloorStyleData.LoadSynchronous();
	
	if (!FloorData)
	{
		UE_LOG(LogTemp, Warning, TEXT("FloorDataAsset failed to load or is null. Cannot generate floor."));
		return;
	}

    // --- PASS 0: DESIGNER OVERRIDES: FORCED PLACEMENTS (NEW) ---
    ExecuteForcedPlacements(RandomStream);
    
    // --- DESIGNER OVERRIDES: FORCED EMPTY CELLS (NEW) ---
    // Mark specific cells as reserved (to be empty) before Pass 1 begins.
    for (const FIntPoint& EmptyCoord : ForcedEmptyFloorCells)
    {
        int32 Index = EmptyCoord.Y * GridSize.X + EmptyCoord.X;
        if (InternalGridState.IsValidIndex(Index) && InternalGridState[Index] == EGridCellType::ECT_Empty)
        {
            // Mark cell as a reserved boundary/empty slot
            InternalGridState[Index] = EGridCellType::ECT_Wall; // Using Wall type to indicate reserved boundary for now
        }
    }


    // --- PASS 1: WEIGHTED AND LARGE MESH PLACEMENT (Modified to check overrides) ---

	for (int32 Y = 0; Y < GridSize.Y; ++Y)
	{
		for (int32 X = 0; X < GridSize.X; ++X)
		{
			const int32 Index = Y * GridSize.X + X;
			
			// If cell is occupied OR marked as forced empty (ECT_Wall), skip to the next
			if (InternalGridState[Index] != EGridCellType::ECT_Empty)
			{
				continue;
			}
			
			// ... (A. Weighted Random Selection remains the same) ...
			const FMeshPlacementInfo* MeshToPlaceInfo = SelectWeightedMesh(FloorData->FloorTilePool, RandomStream);
			
			if (!MeshToPlaceInfo || MeshToPlaceInfo->MeshAsset.IsPending())
			{
				if (MeshToPlaceInfo) MeshToPlaceInfo->MeshAsset.LoadSynchronous();
				continue;
			}
			
			UStaticMesh* Mesh = MeshToPlaceInfo->MeshAsset.Get();
			if (!Mesh) continue;

			bool bCanPlace = true;

			// ... (B. Select Rotation and Calculate Rotated Footprint remains the same) ...
            const int32 RandomRotationIndex = RandomStream.RandRange(0, MeshToPlaceInfo->AllowedRotations.Num() - 1);
			const float YawRotation = (float)MeshToPlaceInfo->AllowedRotations[RandomRotationIndex];

			FIntPoint RotatedFootprint = MeshToPlaceInfo->GridFootprint;
			if (FMath::IsNearlyEqual(YawRotation, 90.0f) || FMath::IsNearlyEqual(YawRotation, 270.0f))
			{
				RotatedFootprint = FIntPoint(MeshToPlaceInfo->GridFootprint.Y, MeshToPlaceInfo->GridFootprint.X);
			}

			// C. Bounds and Occupancy Check (Crucially checks against all existing occupations, including forced items)
			if (X + RotatedFootprint.X > GridSize.X || Y + RotatedFootprint.Y > GridSize.Y)
			{
				bCanPlace = false;
			}
			
			if (bCanPlace)
			{
				for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
				{
					for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
					{
						int32 FootIndex = (Y + FootY) * GridSize.X + (X + FootX);
						
						// The main check: if the cell is NOT empty (it could be ECT_FloorMesh, ECT_Wall/Forced Empty)
						if (InternalGridState.IsValidIndex(FootIndex) && InternalGridState[FootIndex] != EGridCellType::ECT_Empty)
						{
							bCanPlace = false;
							break;
						}
					}
					if (!bCanPlace) break;
				}
			}

			// D. Placement and Grid Marking (remains the same)
			if (bCanPlace)
			{
				UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Mesh);
				if (HISM)
				{
					FVector CenterLocation = FVector(
						(X + RotatedFootprint.X / 2.0f) * CELL_SIZE, 
						(Y + RotatedFootprint.Y / 2.0f) * CELL_SIZE, 
						0.0f
					);
					
					FTransform InstanceTransform(FRotator(0.0f, YawRotation, 0.0f), CenterLocation);
					HISM->AddInstance(InstanceTransform);
					
					// Mark all cells as occupied
					for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
					{
						for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
						{
							int32 FootIndex = (Y + FootY) * GridSize.X + (X + FootX);
							InternalGridState[FootIndex] = EGridCellType::ECT_FloorMesh; 
						}
					}
				}
			}
		}
	}


    // --- PASS 2: GAP FILLING WITH DEFAULT 1x1 TILE (Modified to respect forced empty cells) ---
    
    UStaticMesh* FillerMesh = FloorData->DefaultFillerTile.LoadSynchronous();
    if (FillerMesh)
    {
        UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(FillerMesh);
        
        for (int32 Y = 0; Y < GridSize.Y; ++Y)
        {
            for (int32 X = 0; X < GridSize.X; ++X)
            {
                int32 Index = Y * GridSize.X + X;
                
                // Only place if the cell is still **completely empty** (not ECT_Wall/Forced Empty)
                if (InternalGridState[Index] == EGridCellType::ECT_Empty)
                {
                    // Placement is trivial since it's a 1x1 tile
                    FVector CenterLocation = FVector(
                        (X + 0.5f) * CELL_SIZE, 
                        (Y + 0.5f) * CELL_SIZE, 
                        0.0f
                    );
                    
                    FTransform InstanceTransform(FRotator::ZeroRotator, CenterLocation);
                    HISM->AddInstance(InstanceTransform);
                    
                    // Mark cell as ECT_FloorMesh, it is now filled
                    InternalGridState[Index] = EGridCellType::ECT_FloorMesh; 
                }
            }
        }
    }
}

void AMasterRoom::GenerateWallsAndDoors()
{
	// Implementation to follow: 1D wall packing logic
}

void AMasterRoom::ExecuteForcedPlacements(FRandomStream& Stream)
{
	if (!RoomDataAsset) return;

	const FIntPoint GridSize = RoomDataAsset->GridSize;
	
	// Iterate through the map of designer-forced placements (Pass 0)
	for (const auto& Pair : ForcedInteriorPlacements)
	{
		const FIntPoint StartCoord = Pair.Key;
		const FMeshPlacementInfo& MeshToPlaceInfo = Pair.Value;
		
		bool bCanPlace = true; // Assume placement is possible until proven otherwise

		// 1. Load Mesh and Check Validity
		UStaticMesh* Mesh = MeshToPlaceInfo.MeshAsset.LoadSynchronous();
		if (!Mesh)
		{
			UE_LOG(LogTemp, Warning, TEXT("Forced Placement failed: Mesh asset for (%d, %d) is NULL. Skipping."), StartCoord.X, StartCoord.Y);
			continue;
		}

		// 2. Select Rotation and Calculate Rotated Footprint (Uses Stream for rotation)
		const int32 RandomRotationIndex = Stream.RandRange(0, MeshToPlaceInfo.AllowedRotations.Num() - 1);
		const float YawRotation = (float)MeshToPlaceInfo.AllowedRotations[RandomRotationIndex];

		FIntPoint RotatedFootprint = MeshToPlaceInfo.GridFootprint;
		if (FMath::IsNearlyEqual(YawRotation, 90.0f) || FMath::IsNearlyEqual(YawRotation, 270.0f))
		{
			// Swap dimensions for 90 or 270 degree rotation
			RotatedFootprint = FIntPoint(MeshToPlaceInfo.GridFootprint.Y, MeshToPlaceInfo.GridFootprint.X);
		}

		// 3. Bounds Check
		if (StartCoord.X < 0 || StartCoord.Y < 0 || 
			StartCoord.X + RotatedFootprint.X > GridSize.X || 
			StartCoord.Y + RotatedFootprint.Y > GridSize.Y)
		{
			bCanPlace = false;
			UE_LOG(LogTemp, Warning, TEXT("Forced Placement failed: Mesh at (%d, %d) is out of bounds."), StartCoord.X, StartCoord.Y);
		}
		
		// 4. Overlap Check (Checks against previously placed forced items)
		if (bCanPlace)
		{
			for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
			{
				for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
				{
					int32 FootIndex = (StartCoord.Y + FootY) * GridSize.X + (StartCoord.X + FootX);
					
					// If the target cell is already occupied (by another forced placement), fail.
					if (InternalGridState.IsValidIndex(FootIndex) && InternalGridState[FootIndex] != EGridCellType::ECT_Empty)
					{
						bCanPlace = false;
						UE_LOG(LogTemp, Warning, TEXT("Forced Placement failed: Mesh at (%d, %d) overlaps existing forced item."), StartCoord.X, StartCoord.Y);
						break;
					}
				}
				if (!bCanPlace) break;
			}
		}

		// 5. Placement and Grid Marking (Executed ONLY if all checks passed)
		if (bCanPlace)
		{
			UHierarchicalInstancedStaticMeshComponent* HISM = GetOrCreateHISM(Mesh);
			if (HISM)
			{
				// Calculate position (Center Pivot assumed)
				FVector CenterLocation = FVector(
					(StartCoord.X + RotatedFootprint.X / 2.0f) * CELL_SIZE, 
					(StartCoord.Y + RotatedFootprint.Y / 2.0f) * CELL_SIZE, 
					0.0f
				);
				
				FTransform InstanceTransform(FRotator(0.0f, YawRotation, 0.0f), CenterLocation);
				
				// CRITICAL: Add the instance visually
				HISM->AddInstance(InstanceTransform);
				
				// CRITICAL: Mark all covered cells as occupied (Red in debug view)
				for (int32 FootY = 0; FootY < RotatedFootprint.Y; ++FootY)
				{
					for (int32 FootX = 0; FootX < RotatedFootprint.X; ++FootX)
					{
						int32 FootIndex = (StartCoord.Y + FootY) * GridSize.X + (StartCoord.X + FootX);
						
						if (InternalGridState.IsValidIndex(FootIndex)) 
						{
							InternalGridState[FootIndex] = EGridCellType::ECT_FloorMesh; 
						}
					}
				}
			}
		}
	}
}

// Editor-only overrides for lifecycle management
#if WITH_EDITOR
void AMasterRoom::PostLoad()
{
	Super::PostLoad();
	// Draw the debug grid when the actor is loaded in the editor
	if (GIsEditor)
	{
		DrawDebugGrid();
	}
}
#endif

