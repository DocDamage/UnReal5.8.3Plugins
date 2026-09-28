#include "DocTerminalTypes.h"

namespace DocTerminalUtils
{
	bool NormalizeVirtualPath(const FString& InPath, FString& OutNormalizedPath)
	{
		OutNormalizedPath.Reset();

		FString Trimmed = InPath.TrimStartAndEnd();
		if (Trimmed.IsEmpty())
		{
			return false;
		}

		// Disallow illegal characters
		const TCHAR IllegalChars[] = TEXT("*?\"<>|:\0");
		for (int32 i = 0; IllegalChars[i] != 0; ++i)
		{
			if (Trimmed.Contains(FString(1, &IllegalChars[i])))
			{
				return false;
			}
		}

		// Standardize slashes
		FString Standardized = Trimmed.Replace(TEXT("\\"), TEXT("/"));

		TArray<FString> RawSegments;
		Standardized.ParseIntoArray(RawSegments, TEXT("/"), true);

		TArray<FString> Stack;
		for (const FString& Seg : RawSegments)
		{
			if (Seg == TEXT("."))
			{
				continue;
			}
			else if (Seg == TEXT(".."))
			{
				if (Stack.Num() == 0)
				{
					// Traversal outside root attempted!
					return false;
				}
				Stack.Pop();
			}
			else
			{
				Stack.Add(Seg);
			}
		}

		// Depth limit: max 8 nested directories
		if (Stack.Num() > 8)
		{
			return false;
		}

		if (Stack.Num() == 0)
		{
			OutNormalizedPath = TEXT("/");
			return true;
		}

		OutNormalizedPath = TEXT("/") + FString::Join(Stack, TEXT("/"));
		return true;
	}
}
