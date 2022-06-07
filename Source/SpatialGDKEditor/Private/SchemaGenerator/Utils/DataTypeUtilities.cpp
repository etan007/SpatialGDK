// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "DataTypeUtilities.h"

#include "Algo/Transform.h"
#include "Internationalization/Regex.h"

#include "SpatialGDKEditorSchemaGenerator.h"
#include "Utils/GDKPropertyMacros.h"

// Regex pattern matcher to match alphanumeric characters.
const FRegexPattern AlphanumericPattern(TEXT("[A-Za-z0-9]"));

FString GetEnumDataType(const GDK_PROPERTY(EnumProperty) * EnumProperty)
{
	FString DataType;

	if (EnumProperty->ElementSize < 4)
	{
		// schema types don't include support for 8 or 16 bit data types
		DataType = TEXT("uint32");
	}
	else
	{
		DataType = EnumProperty->GetUnderlyingProperty()->GetCPPType();
	}

	return DataType;
}

FString UnrealNameToSchemaName(const FString& UnrealName, bool bWarnAboutRename /* = false */)
{
	FString Sanitized = AlphanumericSanitization(UnrealName);
	if (Sanitized.IsValidIndex(0) && FChar::IsDigit(Sanitized[0]))
	{
		FString Result = TEXT("ZZ") + Sanitized;
		if (bWarnAboutRename)
		{
			UE_LOG(LogSpatialGDKSchemaGenerator, Warning,
				   TEXT("%s starts with a digit (potentially after removing non-alphanumeric characters), so its schema name was changed "
						"to %s instead. To remove this warning, rename your asset."),
				   *UnrealName, *Result);
		}
		return Result;
	}
	return Sanitized;
}

FString AlphanumericSanitization(const FString& InString)
{
	FRegexMatcher AlphanumericPatternMatcher(AlphanumericPattern, InString);

	FString SanitizedString;

	while (AlphanumericPatternMatcher.FindNext())
	{
		int32 NextCharacter = AlphanumericPatternMatcher.GetMatchBeginning();
		SanitizedString += InString[NextCharacter];
	}

	return SanitizedString;
}

FString UnrealNameToSchemaComponentName(const FString& UnrealName)
{
	FString SchemaTypeName = UnrealNameToSchemaName(UnrealName);
	if (!SchemaTypeName.IsEmpty())
	{
		SchemaTypeName[0] = FChar::ToUpper(SchemaTypeName[0]);
	}
	return SchemaTypeName;
}

FString SchemaReplicatedDataName(EReplicatedPropertyGroup Group, UClass* Class)
{
	return FString::Printf(TEXT("%s%s"), *UnrealNameToSchemaComponentName(ClassPathToSchemaName[Class->GetPathName()]),
						   *GetReplicatedPropertyGroupName(Group));
}

FString SchemaFieldName(const TSharedPtr<FUnrealProperty> Property)
{
	// Transform the property chain into a chain of names.
	TArray<FString> ChainNames;
	Algo::Transform(GetPropertyChain(Property), ChainNames, [](const TSharedPtr<FUnrealProperty>& Property) -> FString {
		FString PropName = Property->Property->GetName().ToLower();
		if (Property->Property->ArrayDim > 1)
		{
			PropName.Append(FString::FromInt(Property->StaticArrayIndex));
		}
		return UnrealNameToSchemaName(PropName);
	});

	// Prefix is required to disambiguate between properties in the generated code and UActorComponent/UObject properties
	// which the generated code extends :troll:.
	FString FieldName = FString::Join(ChainNames, TEXT("_"));
	return FieldName;
}
