// Copyright ProjectLogos

#pragma once

#include "CoreMinimal.h"
#include <GameFramework/PlayerController.h>
#include "PL_PlayerController.generated.h"


class UInputMappingContext;
class UInputAction;
class UPL_InputConfig;

UCLASS()
class PROJECTLOGOS_API APL_PlayerController : public APlayerController
{
	GENERATED_BODY()
	
public:
	APL_PlayerController();
};
