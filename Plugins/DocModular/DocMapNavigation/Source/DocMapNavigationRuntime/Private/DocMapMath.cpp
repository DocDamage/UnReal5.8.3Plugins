#include "DocMapMath.h"
#include "Engine/World.h"
#include "Misc/DataValidation.h"

#include UE_INLINE_GENERATED_CPP_BY_NAME(DocMapMath)

namespace DocMapTags
{
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Marker, "Map.Marker", "DocMapNavigation marker categories");
	UE_DEFINE_GAMEPLAY_TAG(Marker_Objective, "Map.Marker.Objective");
	UE_DEFINE_GAMEPLAY_TAG(Marker_Location, "Map.Marker.Location");
	UE_DEFINE_GAMEPLAY_TAG(Marker_Actor, "Map.Marker.Actor");
	UE_DEFINE_GAMEPLAY_TAG(Marker_Waypoint, "Map.Marker.Waypoint");
	UE_DEFINE_GAMEPLAY_TAG(Marker_FastTravel, "Map.Marker.FastTravel");
	UE_DEFINE_GAMEPLAY_TAG_COMMENT(Layer, "Map.Layer", "Map layers");
}

namespace DocMapPrivate
{
	bool Finite(const FVector& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y) && FMath::IsFinite(V.Z); }
	bool Finite2(const FVector2D& V) { return FMath::IsFinite(V.X) && FMath::IsFinite(V.Y); }
	constexpr double BasisTolerance = 1.e-4;
}

bool FDocMapCoordinateTransform::Validate(FString& OutError) const
{
	using namespace DocMapPrivate;
	if (!Finite(Origin) || !Finite(U) || !Finite(V) || !Finite(N) || !FMath::IsFinite(LengthU) || !FMath::IsFinite(LengthV))
	{
		OutError = TEXT("Non-finite transform");
		return false;
	}
	if (LengthU <= 0.0 || LengthV <= 0.0)
	{
		OutError = TEXT("Map extents must be positive");
		return false;
	}
	const bool bUnit = FMath::IsNearlyEqual(U.Size(), 1.0, BasisTolerance) && FMath::IsNearlyEqual(V.Size(), 1.0, BasisTolerance) && FMath::IsNearlyEqual(N.Size(), 1.0, BasisTolerance);
	const bool bOrtho = FMath::Abs(U | V) < BasisTolerance && FMath::Abs(U | N) < BasisTolerance && FMath::Abs(V | N) < BasisTolerance;
	if (!bUnit || !bOrtho)
	{
		OutError = TEXT("Basis must be orthonormal (degenerate or skewed basis rejected)");
		return false;
	}
	return true;
}

#if WITH_EDITOR
EDataValidationResult UDocMapDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	FString Error;
	if (MapId.IsNone())
	{
		Context.AddError(FText::FromString(TEXT("MapId is required")));
		Result = EDataValidationResult::Invalid;
	}
	if (!Transform.Validate(Error))
	{
		Context.AddError(FText::FromString(Error));
		Result = EDataValidationResult::Invalid;
	}
	TSet<FName> SeenFloors;
	for (const FDocMapFloorDefinition& Floor : this->Floors)
	{
		bool bDuplicate = false;
		SeenFloors.Add(Floor.FloorId, &bDuplicate);
		if (Floor.FloorId.IsNone() || bDuplicate || Floor.MinZ >= Floor.MaxZ)
		{
			Context.AddError(FText::FromString(FString::Printf(TEXT("Floor %s: unique id and MinZ < MaxZ required"), *Floor.FloorId.ToString())));
			Result = EDataValidationResult::Invalid;
		}
	}
	if (MinZoom <= 0.f || MinZoom > MaxZoom)
	{
		Context.AddError(FText::FromString(TEXT("Zoom limits invalid")));
		Result = EDataValidationResult::Invalid;
	}
	return Result;
}
#endif

FDocMapPoint UDocMapMath::WorldToNormalized(const FDocMapCoordinateTransform& Transform, FVector World)
{
	FDocMapPoint Point;
	if (!Transform.Validate(Point.Error))
	{
		return Point;
	}
	if (!DocMapPrivate::Finite(World))
	{
		Point.Error = TEXT("Non-finite world location");
		return Point;
	}
	const FVector Relative = World - Transform.Origin; // doubles (LWC) before any widget conversion
	Point.Normalized = FVector2D((Relative | Transform.U) / Transform.LengthU, (Relative | Transform.V) / Transform.LengthV);
	Point.Height = Relative | Transform.N;
	const bool bInside = Point.Normalized.X >= 0.0 && Point.Normalized.X <= 1.0 && Point.Normalized.Y >= 0.0 && Point.Normalized.Y <= 1.0;
	Point.Status = bInside ? EDocMapPointStatus::Inside : EDocMapPointStatus::Outside;
	Point.DisplayClamped = FVector2D(FMath::Clamp(Point.Normalized.X, 0.0, 1.0), FMath::Clamp(Point.Normalized.Y, 0.0, 1.0));
	return Point;
}

bool UDocMapMath::NormalizedToWorld(const FDocMapCoordinateTransform& Transform, FVector2D Normalized, double Height, FVector& OutWorld, FString& OutError)
{
	if (!Transform.Validate(OutError))
	{
		return false;
	}
	if (!DocMapPrivate::Finite2(Normalized) || !FMath::IsFinite(Height))
	{
		OutError = TEXT("Non-finite map coordinate or height");
		return false;
	}
	OutWorld = Transform.Origin + Normalized.X * Transform.LengthU * Transform.U + Normalized.Y * Transform.LengthV * Transform.V + Height * Transform.N;
	return true;
}

FVector2D UDocMapMath::PixelsToNormalized(FVector2D Pixels, FVector2D TexturePixels)
{
	return (TexturePixels.X > 0 && TexturePixels.Y > 0) ? Pixels / TexturePixels : FVector2D::ZeroVector;
}

namespace DocMapPrivate
{
	bool GeometryValid(const FDocMapViewGeometry& G)
	{
		return G.WidgetSize.X > 0 && G.WidgetSize.Y > 0 && G.DPIScale > 0.f && G.Zoom > 0.f && Finite2(G.Center) && FMath::IsFinite(G.RotationDegrees);
	}
}

bool UDocMapMath::MapToWidget(const FDocMapViewGeometry& G, FVector2D Normalized, FVector2D& OutWidget)
{
	if (!DocMapPrivate::GeometryValid(G) || !DocMapPrivate::Finite2(Normalized))
	{
		return false;
	}
	// Map units → widget units at zoom 1 fit the smaller widget dimension; then zoom, rotate around centre, DPI.
	const double Fit = FMath::Min(G.WidgetSize.X, G.WidgetSize.Y);
	const FVector2D Offset = (Normalized - G.Center) * Fit * G.Zoom;
	const double Rad = FMath::DegreesToRadians((double)G.RotationDegrees);
	const double C = FMath::Cos(Rad), S = FMath::Sin(Rad);
	const FVector2D Rotated(Offset.X * C - Offset.Y * S, Offset.X * S + Offset.Y * C);
	OutWidget = (G.WidgetSize * 0.5 + Rotated) * G.DPIScale;
	return true;
}

bool UDocMapMath::WidgetToMap(const FDocMapViewGeometry& G, FVector2D Widget, FVector2D& OutNormalized)
{
	if (!DocMapPrivate::GeometryValid(G) || !DocMapPrivate::Finite2(Widget))
	{
		return false;
	}
	const double Fit = FMath::Min(G.WidgetSize.X, G.WidgetSize.Y);
	const FVector2D Rotated = Widget / G.DPIScale - G.WidgetSize * 0.5;
	const double Rad = FMath::DegreesToRadians((double)G.RotationDegrees);
	const double C = FMath::Cos(Rad), S = FMath::Sin(Rad);
	const FVector2D Offset(Rotated.X * C + Rotated.Y * S, -Rotated.X * S + Rotated.Y * C);
	OutNormalized = G.Center + Offset / (Fit * G.Zoom);
	return true;
}

FName UDocMapMath::ResolveFloor(const TArray<FDocMapFloorDefinition>& Floors, double Z, FName CurrentFloor, double Hysteresis)
{
	if (!FMath::IsFinite(Z))
	{
		return NAME_None;
	}
	if (!CurrentFloor.IsNone())
	{
		if (const FDocMapFloorDefinition* Current = Floors.FindByPredicate([CurrentFloor](const FDocMapFloorDefinition& F) { return F.FloorId == CurrentFloor; }))
		{
			if (Z >= Current->MinZ - Hysteresis && Z < Current->MaxZ + Hysteresis)
			{
				return CurrentFloor; // stay while within the widened band (no stair flicker)
			}
		}
	}
	const FDocMapFloorDefinition* Best = nullptr;
	for (const FDocMapFloorDefinition& Floor : Floors)
	{
		if (Z >= Floor.MinZ && Z < Floor.MaxZ) // half-open: the top boundary belongs to the band above
		{
			if (!Best || Floor.Priority > Best->Priority || (Floor.Priority == Best->Priority && Floor.FloorId.LexicalLess(Best->FloorId)))
			{
				Best = &Floor;
			}
		}
	}
	return Best ? Best->FloorId : NAME_None;
}

bool UDocMapMath::ComputeBearing(FVector From, float HeadingYaw, FVector Target, float NorthYaw, FDocCompassEntry& OutEntry)
{
	if (!DocMapPrivate::Finite(From) || !DocMapPrivate::Finite(Target) || !FMath::IsFinite(HeadingYaw) || !FMath::IsFinite(NorthYaw))
	{
		return false;
	}
	const FVector Delta = Target - From;
	const FVector2D Flat(Delta.X, Delta.Y);
	OutEntry.Distance = Delta.Size();
	if (Flat.IsNearlyZero())
	{
		OutEntry.bSamePosition = true;
		OutEntry.RelativeBearing = 0.f;
		OutEntry.CompassBearing = 0.f;
		OutEntry.Direction = FVector2D::ZeroVector;
		return true;
	}
	const double TargetYaw = FMath::RadiansToDegrees(FMath::Atan2(Flat.Y, Flat.X));
	double Relative = FRotator::NormalizeAxis(TargetYaw - HeadingYaw);
	if (Relative <= -180.0) { Relative = 180.0; } // (-180, 180]
	OutEntry.RelativeBearing = (float)Relative;
	double Compass = FMath::Fmod(TargetYaw - NorthYaw, 360.0);
	if (Compass < 0.0) { Compass += 360.0; }
	OutEntry.CompassBearing = (float)Compass;
	const double Rad = FMath::DegreesToRadians(Relative);
	OutEntry.Direction = FVector2D(FMath::Cos(Rad), FMath::Sin(Rad));
	return true;
}

bool UDocMapMath::ProjectIndicator(const FDocMapViewProjection& View, FVector Target, FDocOffscreenIndicator& Out)
{
	if (View.ViewportSize.X <= 0 || View.ViewportSize.Y <= 0 || View.HorizontalFOV <= 0.f || View.HorizontalFOV >= 180.f || !DocMapPrivate::Finite(Target))
	{
		return false; // invalid geometry: no division by zero
	}
	FVector2D SafeMin = View.SafeMin;
	FVector2D SafeMax = View.SafeMax;
	if (SafeMax.X <= SafeMin.X || SafeMax.Y <= SafeMin.Y)
	{
		SafeMin = FVector2D::ZeroVector;
		SafeMax = View.ViewportSize;
	}
	const FRotationMatrix Rot(View.ViewRotation);
	const FVector Local = Rot.InverseTransformVector(Target - View.ViewLocation); // X forward, Y right, Z up
	const FVector2D Center = View.ViewportSize * 0.5;
	const double HalfTan = FMath::Tan(FMath::DegreesToRadians((double)View.HorizontalFOV * 0.5));
	const double FocalX = Center.X / HalfTan;
	Out.bBehindCamera = Local.X <= KINDA_SMALL_NUMBER;
	FVector2D Screen;
	if (!Out.bBehindCamera)
	{
		Screen = Center + FVector2D(Local.Y / Local.X * FocalX, -Local.Z / Local.X * FocalX);
		Out.bOnScreen = Screen.X >= SafeMin.X && Screen.X <= SafeMax.X && Screen.Y >= SafeMin.Y && Screen.Y <= SafeMax.Y;
	}
	else
	{
		// Behind: point the indicator along the lateral direction (never mirrored into the view).
		FVector2D Dir(Local.Y, -Local.Z);
		if (Dir.IsNearlyZero()) { Dir = FVector2D(0, 1); } // directly behind: bottom edge
		Screen = Center + Dir.GetSafeNormal() * View.ViewportSize.GetMax();
		Out.bOnScreen = false;
	}
	FVector2D Dir = Screen - Center;
	Out.Angle = FMath::IsNearlyZero(Dir.SizeSquared()) ? 0.f : (float)FMath::RadiansToDegrees(FMath::Atan2(Dir.X, -Dir.Y));
	if (!Out.bOnScreen)
	{
		// Clamp along the ray from the centre to the safe rectangle.
		const FVector2D HalfSafe = (SafeMax - SafeMin) * 0.5;
		const FVector2D SafeCenter = (SafeMin + SafeMax) * 0.5;
		const FVector2D D = Screen - SafeCenter;
		const double Sx = FMath::Abs(D.X) > KINDA_SMALL_NUMBER ? HalfSafe.X / FMath::Abs(D.X) : TNumericLimits<double>::Max();
		const double Sy = FMath::Abs(D.Y) > KINDA_SMALL_NUMBER ? HalfSafe.Y / FMath::Abs(D.Y) : TNumericLimits<double>::Max();
		Screen = SafeCenter + D * FMath::Min(1.0, FMath::Min(Sx, Sy));
	}
	Out.ScreenPosition = Screen;
	return true;
}

FVector UDocMapMath::ToAbsolute(const UWorld* World, const FVector& EngineLocation)
{
	return World ? EngineLocation + FVector(World->OriginLocation) : EngineLocation;
}

FVector UDocMapMath::ToEngine(const UWorld* World, const FVector& AbsoluteLocation)
{
	return World ? AbsoluteLocation - FVector(World->OriginLocation) : AbsoluteLocation;
}
