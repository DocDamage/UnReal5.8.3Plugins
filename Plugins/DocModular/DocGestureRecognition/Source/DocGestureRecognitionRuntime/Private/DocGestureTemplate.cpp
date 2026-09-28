#include "DocGestureTemplate.h"
#include "Misc/SecureHash.h"
#include "Algo/Reverse.h"

// ---------------------------------------------------------------------------------------------
// Template
// ---------------------------------------------------------------------------------------------

FDocSystemResult UDocGestureTemplate::ValidateTemplate() const
{
	if (GestureId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Template has no GestureId"));
	}
	if (CanonicalPoints.Num() < 2 || CanonicalPoints.Num() > 1000)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("%s needs 2..1000 canonical points"), *GestureId.ToString()));
	}
	double Length = 0.0;
	for (int32 Index = 0; Index < CanonicalPoints.Num(); ++Index)
	{
		const FVector2D& P = CanonicalPoints[Index];
		if (!FMath::IsFinite(P.X) || !FMath::IsFinite(P.Y))
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("%s has a non-finite point"), *GestureId.ToString()));
		}
		if (Index > 0)
		{
			Length += FVector2D::Distance(CanonicalPoints[Index - 1], P);
		}
	}
	if (Length <= 1.0e-6)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("%s has zero path length"), *GestureId.ToString()));
	}
	if (ResampleCount < 8 || ResampleCount > 256)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("ResampleCount must be in [8, 256]"));
	}
	if (!FMath::IsFinite(MinSimilarityThreshold) || MinSimilarityThreshold <= 0.0f || MinSimilarityThreshold > 1.0f
		|| !FMath::IsFinite(MinRunnerUpMargin) || MinRunnerUpMargin < 0.0f || MinRunnerUpMargin >= 1.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Threshold must be in (0, 1] and margin in [0, 1)"));
	}
	if (!FMath::IsFinite(RotationSearchDegrees) || RotationSearchDegrees < 0.0f || RotationSearchDegrees > 180.0f
		|| !FMath::IsFinite(MinPathLength) || MinPathLength < 0.0f)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Rotation search must be in [0, 180] and MinPathLength >= 0"));
	}
	return FDocSystemResult::MakeSuccess();
}

FString UDocGestureTemplate::ComputeContentHash() const
{
	FString Signature = FString::Printf(TEXT("A%d|%s|%d|%d|%d|%.4f|%d|%.4f|%.4f|%.4f|%d"),
		FDocGestureRecognizer::AlgorithmVersion, *GestureId.ToString(), ResampleCount, static_cast<int32>(ScalePolicy),
		bRotationInvariant ? 1 : 0, RotationSearchDegrees, bDirectionInvariant ? 1 : 0,
		MinSimilarityThreshold, MinRunnerUpMargin, MinPathLength, bAllowAccessibleAlternative ? 1 : 0);
	for (const FVector2D& P : CanonicalPoints)
	{
		Signature += FString::Printf(TEXT("|%.6f,%.6f"), P.X, P.Y);
	}
	return FMD5::HashAnsiString(*Signature);
}

bool UDocGestureTemplate::ResamplePoints(const TArray<FVector2D>& InPoints, int32 TargetCount, TArray<FVector2D>& OutPoints)
{
	OutPoints.Reset();
	if (InPoints.Num() < 2 || TargetCount < 2)
	{
		return false;
	}
	// Drop non-finite points and consecutive near-duplicates (stationary pointers).
	TArray<FVector2D> Filtered;
	Filtered.Reserve(InPoints.Num());
	for (const FVector2D& Pt : InPoints)
	{
		if (!FMath::IsFinite(Pt.X) || !FMath::IsFinite(Pt.Y))
		{
			continue;
		}
		if (Filtered.Num() == 0 || FVector2D::DistSquared(Filtered.Last(), Pt) > 1.0e-12)
		{
			Filtered.Add(Pt);
		}
	}
	if (Filtered.Num() < 2)
	{
		return false;
	}
	double TotalLength = 0.0;
	for (int32 Index = 1; Index < Filtered.Num(); ++Index)
	{
		TotalLength += FVector2D::Distance(Filtered[Index - 1], Filtered[Index]);
	}
	if (!FMath::IsFinite(TotalLength) || TotalLength <= 1.0e-9)
	{
		return false;
	}

	const double Interval = TotalLength / static_cast<double>(TargetCount - 1);
	OutPoints.Reserve(TargetCount);
	OutPoints.Add(Filtered[0]);
	FVector2D Prev = Filtered[0];
	double Accumulated = 0.0;
	int32 Index = 1;
	// Each iteration either emits a point (bounded by TargetCount) or advances Index (bounded by the input).
	while (Index < Filtered.Num() && OutPoints.Num() < TargetCount)
	{
		const FVector2D Next = Filtered[Index];
		const double Segment = FVector2D::Distance(Prev, Next);
		if (Segment > 0.0 && Accumulated + Segment >= Interval)
		{
			const double T = (Interval - Accumulated) / Segment;
			const FVector2D NewPoint = Prev + (Next - Prev) * T;
			OutPoints.Add(NewPoint);
			Prev = NewPoint;
			Accumulated = 0.0;
		}
		else
		{
			Accumulated += Segment;
			Prev = Next;
			++Index;
		}
	}
	while (OutPoints.Num() < TargetCount)
	{
		OutPoints.Add(Filtered.Last());
	}
	OutPoints.SetNum(TargetCount);
	return true;
}

FVector2D UDocGestureTemplate::ComputeCentroid(const TArray<FVector2D>& InPoints)
{
	if (InPoints.Num() == 0)
	{
		return FVector2D::ZeroVector;
	}
	FVector2D Sum = FVector2D::ZeroVector;
	for (const FVector2D& Pt : InPoints)
	{
		Sum += Pt;
	}
	return Sum / static_cast<double>(InPoints.Num());
}

void UDocGestureTemplate::TranslateToOrigin(TArray<FVector2D>& InOutPoints)
{
	const FVector2D Centroid = ComputeCentroid(InOutPoints);
	for (FVector2D& Pt : InOutPoints)
	{
		Pt -= Centroid;
	}
}

void UDocGestureTemplate::ScalePoints(TArray<FVector2D>& InOutPoints, EDocGestureScalePolicy Policy, float TargetBoxSize)
{
	if (InOutPoints.Num() == 0)
	{
		return;
	}
	FVector2D Min = InOutPoints[0];
	FVector2D Max = InOutPoints[0];
	for (const FVector2D& Pt : InOutPoints)
	{
		Min.X = FMath::Min(Min.X, Pt.X);
		Min.Y = FMath::Min(Min.Y, Pt.Y);
		Max.X = FMath::Max(Max.X, Pt.X);
		Max.Y = FMath::Max(Max.Y, Pt.Y);
	}
	const double Width = Max.X - Min.X;
	const double Height = Max.Y - Min.Y;
	const double Larger = FMath::Max(Width, Height);
	if (Larger <= 1.0e-9)
	{
		return;
	}
	// Non-uniform stretching of a near-1D stroke would amplify noise; fall back to uniform.
	const bool bUniform = Policy == EDocGestureScalePolicy::UniformPreserveAspect || FMath::Min(Width, Height) < 0.1 * Larger;
	if (bUniform)
	{
		const double Scale = TargetBoxSize / Larger;
		for (FVector2D& Pt : InOutPoints)
		{
			Pt *= Scale;
		}
		return;
	}
	const double ScaleX = TargetBoxSize / Width;
	const double ScaleY = TargetBoxSize / Height;
	for (FVector2D& Pt : InOutPoints)
	{
		Pt.X *= ScaleX;
		Pt.Y *= ScaleY;
	}
}

void UDocGestureTemplate::RotatePoints(TArray<FVector2D>& InOutPoints, float AngleRadians)
{
	const double C = FMath::Cos(static_cast<double>(AngleRadians));
	const double S = FMath::Sin(static_cast<double>(AngleRadians));
	for (FVector2D& Pt : InOutPoints)
	{
		const double X = Pt.X * C - Pt.Y * S;
		const double Y = Pt.X * S + Pt.Y * C;
		Pt.X = X;
		Pt.Y = Y;
	}
}

float UDocGestureTemplate::ComputeIndicativeAngle(const TArray<FVector2D>& InPoints)
{
	if (InPoints.Num() == 0)
	{
		return 0.0f;
	}
	const FVector2D Centroid = ComputeCentroid(InPoints);
	return static_cast<float>(FMath::Atan2(InPoints[0].Y - Centroid.Y, InPoints[0].X - Centroid.X));
}

float UDocGestureTemplate::ComputeAveragePointDistance(const TArray<FVector2D>& PointsA, const TArray<FVector2D>& PointsB)
{
	if (PointsA.Num() == 0 || PointsA.Num() != PointsB.Num())
	{
		return 1.0e6f;
	}
	double Sum = 0.0;
	for (int32 Index = 0; Index < PointsA.Num(); ++Index)
	{
		Sum += FVector2D::Distance(PointsA[Index], PointsB[Index]);
	}
	return static_cast<float>(Sum / static_cast<double>(PointsA.Num()));
}

// ---------------------------------------------------------------------------------------------
// Set
// ---------------------------------------------------------------------------------------------

void UDocGestureTemplateSet::AddTemplate(UDocGestureTemplate* Template)
{
	if (Template && !Templates.Contains(Template))
	{
		Templates.Add(Template);
	}
}

UDocGestureTemplate* UDocGestureTemplateSet::FindTemplate(FName InGestureId) const
{
	for (const TObjectPtr<UDocGestureTemplate>& Template : Templates)
	{
		if (Template && Template->GestureId == InGestureId)
		{
			return Template.Get();
		}
	}
	return nullptr;
}

FDocSystemResult UDocGestureTemplateSet::ValidateSet() const
{
	if (SetId.IsNone())
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Template set has no SetId"));
	}
	if (Templates.IsEmpty() || Templates.Num() > MaxTemplates)
	{
		return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Template set needs 1..%d templates"), MaxTemplates));
	}
	TSet<FName> Ids;
	for (const TObjectPtr<UDocGestureTemplate>& Template : Templates)
	{
		if (!Template)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, TEXT("Template set contains a null entry"));
		}
		const FDocSystemResult Valid = Template->ValidateTemplate();
		if (!Valid.IsSuccess())
		{
			return Valid;
		}
		bool bDuplicate = false;
		Ids.Add(Template->GestureId, &bDuplicate);
		if (bDuplicate)
		{
			return FDocSystemResult::MakeFailure(EDocResultOutcome::InvalidConfiguration, FString::Printf(TEXT("Duplicate GestureId %s"), *Template->GestureId.ToString()));
		}
	}
	return FDocSystemResult::MakeSuccess();
}

TArray<FString> UDocGestureTemplateSet::FindCollisions() const
{
	TArray<FString> Out;
	TArray<FDocGestureCompiledTemplate> Compiled;
	for (const TObjectPtr<UDocGestureTemplate>& Template : Templates)
	{
		FDocGestureCompiledTemplate C;
		if (Template && FDocGestureRecognizer::Compile(*Template, C))
		{
			Compiled.Add(MoveTemp(C));
		}
	}
	for (int32 i = 0; i < Templates.Num(); ++i)
	{
		const UDocGestureTemplate* Source = Templates[i];
		if (!Source)
		{
			continue;
		}
		for (const FDocGestureCompiledTemplate& Target : Compiled)
		{
			if (Target.GestureId == Source->GestureId)
			{
				continue;
			}
			FDocGestureCandidate Candidate;
			int32 Work = 0;
			if (FDocGestureRecognizer::Score(Source->CanonicalPoints, Target, Candidate, Work) && Candidate.Similarity >= Target.MinSimilarityThreshold)
			{
				Out.Add(FString::Printf(TEXT("%s is recognized as %s (similarity %.3f)"), *Source->GestureId.ToString(), *Target.GestureId.ToString(), Candidate.Similarity));
			}
		}
	}
	return Out;
}

// ---------------------------------------------------------------------------------------------
// Recognizer
// ---------------------------------------------------------------------------------------------

float FDocGestureRecognizer::SimilarityFromDistance(float Distance)
{
	// Points live in a unit box, so a mean distance of 0.5 is "unrelated". Similarity, not probability.
	return FMath::Clamp(1.0f - Distance / 0.5f, 0.0f, 1.0f);
}

bool FDocGestureRecognizer::Compile(const UDocGestureTemplate& Template, FDocGestureCompiledTemplate& Out)
{
	Out = FDocGestureCompiledTemplate();
	if (!Template.ValidateTemplate().IsSuccess())
	{
		return false;
	}
	Out.GestureId = Template.GestureId;
	Out.DisplayName = Template.DisplayName;
	Out.ContentHash = Template.ComputeContentHash();
	Out.ResampleCount = Template.ResampleCount;
	Out.ScalePolicy = Template.ScalePolicy;
	Out.bRotationInvariant = Template.bRotationInvariant;
	Out.RotationSearchDegrees = Template.RotationSearchDegrees;
	Out.bDirectionInvariant = Template.bDirectionInvariant;
	Out.MinSimilarityThreshold = Template.MinSimilarityThreshold;
	Out.MinRunnerUpMargin = Template.MinRunnerUpMargin;
	Out.bAllowAccessibleAlternative = Template.bAllowAccessibleAlternative;
	TArray<FVector2D> Normalized;
	if (!Normalize(Template.CanonicalPoints, Out, /*bReverse*/ false, Normalized))
	{
		return false;
	}
	Out.Points = MoveTemp(Normalized);
	return true;
}

bool FDocGestureRecognizer::Normalize(const TArray<FVector2D>& RawPoints, const FDocGestureCompiledTemplate& Settings, bool bReverse, TArray<FVector2D>& Out)
{
	if (!UDocGestureTemplate::ResamplePoints(RawPoints, Settings.ResampleCount, Out))
	{
		return false;
	}
	if (bReverse)
	{
		Algo::Reverse(Out);
	}
	UDocGestureTemplate::TranslateToOrigin(Out);
	if (Settings.bRotationInvariant)
	{
		UDocGestureTemplate::RotatePoints(Out, -UDocGestureTemplate::ComputeIndicativeAngle(Out));
	}
	UDocGestureTemplate::ScalePoints(Out, Settings.ScalePolicy, 1.0f);
	return true;
}

bool FDocGestureRecognizer::Score(const TArray<FVector2D>& RawPoints, const FDocGestureCompiledTemplate& Template, FDocGestureCandidate& OutCandidate, int32& InOutWork)
{
	OutCandidate = FDocGestureCandidate();
	OutCandidate.GestureId = Template.GestureId;
	OutCandidate.DisplayName = Template.DisplayName;
	if (Template.Points.IsEmpty())
	{
		return false;
	}
	float BestDistance = TNumericLimits<float>::Max();
	bool bAny = false;
	const int32 Variants = Template.bDirectionInvariant ? 2 : 1;
	for (int32 Variant = 0; Variant < Variants; ++Variant)
	{
		TArray<FVector2D> Candidate;
		if (!Normalize(RawPoints, Template, Variant == 1, Candidate))
		{
			continue;
		}
		auto Eval = [&](float AngleRadians) -> float
		{
			++InOutWork;
			if (AngleRadians == 0.0f)
			{
				return UDocGestureTemplate::ComputeAveragePointDistance(Candidate, Template.Points);
			}
			TArray<FVector2D> Rotated = Candidate;
			UDocGestureTemplate::RotatePoints(Rotated, AngleRadians);
			return UDocGestureTemplate::ComputeAveragePointDistance(Rotated, Template.Points);
		};

		float VariantBest = Eval(0.0f);
		float VariantAngle = 0.0f;
		if (Template.bRotationInvariant && Template.RotationSearchDegrees > 0.0f)
		{
			// Golden-section search over a bounded range with a fixed iteration count.
			const float Phi = 0.5f * (FMath::Sqrt(5.0f) - 1.0f);
			float A = -FMath::DegreesToRadians(Template.RotationSearchDegrees);
			float B = FMath::DegreesToRadians(Template.RotationSearchDegrees);
			float X1 = B - Phi * (B - A);
			float X2 = A + Phi * (B - A);
			float F1 = Eval(X1);
			float F2 = Eval(X2);
			for (int32 Iteration = 0; Iteration < RotationIterations; ++Iteration)
			{
				if (F1 < F2)
				{
					B = X2; X2 = X1; F2 = F1;
					X1 = B - Phi * (B - A);
					F1 = Eval(X1);
				}
				else
				{
					A = X1; X1 = X2; F1 = F2;
					X2 = A + Phi * (B - A);
					F2 = Eval(X2);
				}
			}
			if (F1 < VariantBest) { VariantBest = F1; VariantAngle = X1; }
			if (F2 < VariantBest) { VariantBest = F2; VariantAngle = X2; }
		}
		if (!bAny || VariantBest < BestDistance)
		{
			BestDistance = VariantBest;
			OutCandidate.bMatchedReversed = Variant == 1;
			OutCandidate.MatchedRotationDegrees = FMath::RadiansToDegrees(VariantAngle);
			bAny = true;
		}
	}
	if (!bAny)
	{
		return false;
	}
	OutCandidate.Distance = BestDistance;
	OutCandidate.Similarity = SimilarityFromDistance(BestDistance);
	return true;
}
