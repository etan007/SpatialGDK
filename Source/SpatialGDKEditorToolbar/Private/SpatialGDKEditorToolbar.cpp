// Copyright (c) Improbable Worlds Ltd, All Rights Reserved

#include "SpatialGDKEditorToolbar.h"

#include "SpatialConstants.cxx"
#include "SpatialConstants.h"

#include "AssetRegistryModule.h"
#include "Async/Async.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "EditorStyleSet.h"
#include "EngineClasses/SpatialWorldSettings.h"
#include "EngineUtils.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GeneralProjectSettings.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformFilemanager.h"
#include "IOSRuntimeSettings.h"
#include "ISettingsContainer.h"
#include "ISettingsModule.h"
#include "ISettingsSection.h"
#include "Interfaces/IProjectManager.h"
#include "Internationalization/Regex.h"
#include "LevelEditor.h"
#include "Misc/FileHelper.h"
#include "Misc/MessageDialog.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Sound/SoundBase.h"
#include "Widgets/DeclarativeSyntaxSupport.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SNotificationList.h"

#include "CloudDeploymentConfiguration.h"
#include "SpatialCommandUtils.h"
#include "SpatialConstants.h"
#include "SpatialGDKCloudDeploymentConfiguration.h"
#include "SpatialGDKDefaultLaunchConfigGenerator.h"
#include "SpatialGDKDefaultWorkerJsonGenerator.h"
#include "SpatialGDKDevAuthTokenGenerator.h"
#include "SpatialGDKEditor.h"
#include "SpatialGDKEditorModule.h"
#include "SpatialGDKEditorPackageAssembly.h"
#include "SpatialGDKEditorSchemaGenerator.h"
#include "SpatialGDKEditorSettings.h"
#include "SpatialGDKEditorSnapshotGenerator.h"
#include "SpatialGDKEditorToolbarCommands.h"
#include "SpatialGDKEditorToolbarStyle.h"
#include "SpatialGDKServicesConstants.h"
#include "SpatialGDKServicesModule.h"
#include "SpatialGDKSettings.h"
#include "TestMapGeneration.h"
#include "Utils/GDKPropertyMacros.h"
#include "Utils/LaunchConfigurationEditor.h"
#include "Utils/SpatialDebugger.h"
#include "Utils/SpatialStatics.h"

DEFINE_LOG_CATEGORY(LogSpatialGDKEditorToolbar);

#define LOCTEXT_NAMESPACE "FSpatialGDKEditorToolbarModule"

FSpatialGDKEditorToolbarModule::FSpatialGDKEditorToolbarModule()
	: AutoStopLocalDeployment(EAutoStopLocalDeploymentMode::Never)
	, bStartingCloudDeployment(false)
	, SpatialDebugger(nullptr)
{
}

void FSpatialGDKEditorToolbarModule::StartupModule()
{
	FSpatialGDKEditorToolbarStyle::Initialize();
	FSpatialGDKEditorToolbarStyle::ReloadTextures();

	FSpatialGDKEditorToolbarCommands::Register();

	PluginCommands = MakeShareable(new FUICommandList);
	MapActions(PluginCommands);
	SetupToolbar(PluginCommands);

	// load sounds
	ExecutionStartSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileStart_Cue.CompileStart_Cue"));
	ExecutionStartSound->AddToRoot();
	ExecutionSuccessSound =
		LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileSuccess_Cue.CompileSuccess_Cue"));
	ExecutionSuccessSound->AddToRoot();
	ExecutionFailSound = LoadObject<USoundBase>(nullptr, TEXT("/Engine/EditorSounds/Notifications/CompileFailed_Cue.CompileFailed_Cue"));
	ExecutionFailSound->AddToRoot();

	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();

	OnPropertyChangedDelegateHandle =
		FCoreUObjectDelegates::OnObjectPropertyChanged.AddRaw(this, &FSpatialGDKEditorToolbarModule::OnPropertyChanged);
	AutoStopLocalDeployment = SpatialGDKEditorSettings->AutoStopLocalDeployment;

	// Check for UseChinaServicesRegion file in the plugin directory to determine the services region.
	bool bUseChinaServicesRegion = FPaths::FileExists(
		FSpatialGDKServicesModule::GetSpatialGDKPluginDirectory(SpatialGDKServicesConstants::UseChinaServicesRegionFilename));
	GetMutableDefault<USpatialGDKSettings>()->SetServicesRegion(bUseChinaServicesRegion ? EServicesRegion::CN : EServicesRegion::Default);

	// This is relying on the module loading phase - SpatialGDKServices module should be already loaded
	FSpatialGDKServicesModule& GDKServices = FModuleManager::GetModuleChecked<FSpatialGDKServicesModule>("SpatialGDKServices");
	LocalDeploymentManager = GDKServices.GetLocalDeploymentManager();
	LocalDeploymentManager->PreInit(GetDefault<USpatialGDKSettings>()->IsRunningInChina());

	LocalReceptionistProxyServerManager = GDKServices.GetLocalReceptionistProxyServerManager();

	OnAutoStartLocalDeploymentChanged();

	// This code block starts a local deployment when loading maps for automation testing
	// However, it is no longer required in 4.25 and beyond, due to the editor flow refactors.
 

	// We try to stop a local deployment either when the appropriate setting is selected, or when running with automation tests
	FEditorDelegates::EndPIE.AddLambda([this](bool bIsSimulatingInEditor) {
		if ((GIsAutomationTesting || AutoStopLocalDeployment == EAutoStopLocalDeploymentMode::OnEndPIE)
			&& LocalDeploymentManager->IsLocalDeploymentRunning())
		{
			AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this] {
				const USpatialGDKEditorSettings* CurSpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
				bool bRuntimeShutdown = CurSpatialGDKEditorSettings->bShutdownRuntimeGracefullyOnPIEExit
											? LocalDeploymentManager->TryStopLocalDeploymentGracefully()
											: LocalDeploymentManager->TryStopLocalDeployment();

				if (!bRuntimeShutdown)
				{
					OnShowFailedNotification(TEXT("Failed to stop local deployment!"));
				}
			});
		}
	});

	LocalDeploymentManager->Init();
	LocalReceptionistProxyServerManager->Init(GetDefault<USpatialGDKEditorSettings>()->LocalReceptionistPort);

	SpatialGDKEditorInstance = FModuleManager::GetModuleChecked<FSpatialGDKEditorModule>("SpatialGDKEditor").GetSpatialGDKEditorInstance();

	// Get notified of map changed events to update worker boundaries in the editor
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	FDelegateHandle OnMapChangedHandle = LevelEditorModule.OnMapChanged().AddRaw(this, &FSpatialGDKEditorToolbarModule::MapChanged);

	if (USpatialStatics::IsSpatialNetworkingEnabled())
	{
		// Grab the runtime and inspector binaries ahead of time so they are ready when the user wants them.
		const FString RuntimeVersion = SpatialGDKEditorSettings->GetSelectedRuntimeVariantVersion().GetVersionForLocal();
		const FString InspectorVersion = SpatialGDKEditorSettings->GetInspectorVersion();

		AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, RuntimeVersion, InspectorVersion] {
			if (!FetchRuntimeBinaryWrapper(RuntimeVersion))
			{
				UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to cache the local runtime binary but failed!"));
			}

			if (!FetchInspectorBinaryWrapper(InspectorVersion))
			{
				UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to cache the local inspector binary but failed!"));
			}
		});
	}
}

void FSpatialGDKEditorToolbarModule::ShutdownModule()
{
	FCoreUObjectDelegates::OnObjectPropertyChanged.Remove(OnPropertyChangedDelegateHandle);

	if (ExecutionStartSound != nullptr)
	{
		if (!GExitPurge)
		{
			ExecutionStartSound->RemoveFromRoot();
		}
		ExecutionStartSound = nullptr;
	}

	if (ExecutionSuccessSound != nullptr)
	{
		if (!GExitPurge)
		{
			ExecutionSuccessSound->RemoveFromRoot();
		}
		ExecutionSuccessSound = nullptr;
	}

	if (ExecutionFailSound != nullptr)
	{
		if (!GExitPurge)
		{
			ExecutionFailSound->RemoveFromRoot();
		}
		ExecutionFailSound = nullptr;
	}

	if (FLevelEditorModule* LevelEditor = FModuleManager::GetModulePtr<FLevelEditorModule>("LevelEditor"))
	{
		LevelEditor->OnMapChanged().RemoveAll(this);
	}

	FSpatialGDKEditorToolbarStyle::Shutdown();
	FSpatialGDKEditorToolbarCommands::Unregister();
}

void FSpatialGDKEditorToolbarModule::PreUnloadCallback()
{
	LocalReceptionistProxyServerManager->TryStopReceptionistProxyServer();

	if (AutoStopLocalDeployment != EAutoStopLocalDeploymentMode::Never)
	{
		if (InspectorProcess.IsSet() && InspectorProcess->Update())
		{
			InspectorProcess->Cancel();
		}
		LocalDeploymentManager->TryStopLocalDeployment();
	}
}

void FSpatialGDKEditorToolbarModule::Tick(float DeltaTime) {}

bool FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator() const
{
	return SpatialGDKEditorInstance.IsValid() && !SpatialGDKEditorInstance.Get()->IsSchemaGeneratorRunning();
}

bool FSpatialGDKEditorToolbarModule::CanExecuteSnapshotGenerator() const
{
	return SpatialGDKEditorInstance.IsValid() && !SpatialGDKEditorInstance.Get()->IsSchemaGeneratorRunning();
}

void FSpatialGDKEditorToolbarModule::MapActions(TSharedPtr<class FUICommandList> InPluginCommands)
{
	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::SchemaGenerateButtonClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchemaFull,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::SchemaGenerateFullButtonClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().DeleteSchemaDatabase,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::DeleteSchemaDatabaseButtonClicked));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().CleanGenerateSchema,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CleanSchemaGenerateButtonClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSchemaGenerator));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateSnapshotButtonClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CanExecuteSnapshotGenerator));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().StartNative, FExecuteAction(),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartNativeCanExecute),
								FIsActionChecked(),
								FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartNativeIsVisible));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().StartLocalSpatialDeployment,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartLocalSpatialDeploymentButtonClicked),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartLocalSpatialDeploymentCanExecute), FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartLocalSpatialDeploymentIsVisible));

	InPluginCommands->MapAction(
		FSpatialGDKEditorToolbarCommands::Get().StartCloudSpatialDeployment,
		FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchOrShowCloudDeployment),
		FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartCloudSpatialDeploymentCanExecute), FIsActionChecked(),
		FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StartCloudSpatialDeploymentIsVisible));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialDeploymentButtonClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialDeploymentCanExecute),
								FIsActionChecked(),
								FIsActionButtonVisible::CreateRaw(this, &FSpatialGDKEditorToolbarModule::StopSpatialDeploymentIsVisible));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchInspectorWebpageButtonClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LaunchInspectorWebpageCanExecute));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().EnableBuildClientWorker,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::OnCheckedBuildClientWorker),
								FCanExecuteAction::CreateStatic(&FSpatialGDKEditorToolbarModule::AreCloudDeploymentPropertiesEditable),
								FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsBuildClientWorkerEnabled));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().EnableBuildSimulatedPlayer,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::OnCheckedSimulatedPlayers),
								FCanExecuteAction::CreateStatic(&FSpatialGDKEditorToolbarModule::AreCloudDeploymentPropertiesEditable),
								FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsSimulatedPlayersEnabled));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().OpenCloudDeploymentWindowAction,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ShowCloudDeploymentDialog),
								FCanExecuteAction());

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().OpenLaunchConfigurationEditorAction,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::OpenLaunchConfigurationEditor),
								FCanExecuteAction());

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().EnableSpatialNetworking,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::OnToggleSpatialNetworking),
								FCanExecuteAction(),
								FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::OnIsSpatialNetworkingEnabled));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().LocalDeployment,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::LocalDeploymentClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::OnIsSpatialNetworkingEnabled),
								FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsLocalDeploymentSelected));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().CloudDeployment,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CloudDeploymentClicked),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsSpatialOSNetFlowConfigurable),
								FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsCloudDeploymentSelected));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().GDKEditorSettings,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::GDKEditorSettingsClicked));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().GDKRuntimeSettings,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::GDKRuntimeSettingsClicked));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().ToggleSpatialDebuggerEditor,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ToggleSpatialDebuggerEditor),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::AllowWorkerBoundaries),
								FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsSpatialDebuggerEditorEnabled));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().ToggleMultiWorkerEditor,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::ToggleMultiworkerEditor),
								FCanExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::OnIsSpatialNetworkingEnabled),
								FIsActionChecked::CreateRaw(this, &FSpatialGDKEditorToolbarModule::IsMultiWorkerEnabled));

	InPluginCommands->MapAction(FSpatialGDKEditorToolbarCommands::Get().GenerateTestMaps,
								FExecuteAction::CreateRaw(this, &FSpatialGDKEditorToolbarModule::GenerateTestMaps));
}

void FSpatialGDKEditorToolbarModule::SetupToolbar(TSharedPtr<class FUICommandList> InPluginCommands)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");
	{
		TSharedPtr<FExtender> MenuExtender = MakeShareable(new FExtender());
		MenuExtender->AddMenuExtension("LevelEditor", EExtensionHook::After, InPluginCommands,
									   FMenuExtensionDelegate::CreateRaw(this, &FSpatialGDKEditorToolbarModule::AddMenuExtension));

		LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
	}

	{
		/*
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension(
			"LevelEditor", EExtensionHook::After, InPluginCommands,
			FToolBarExtensionDelegate::CreateRaw(this, &FSpatialGDKEditorToolbarModule::AddToolbarExtension));

		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);*/
		TSharedPtr<FExtender> ToolbarExtender = MakeShareable(new FExtender);
		ToolbarExtender->AddToolBarExtension(
			"Play", EExtensionHook::After, InPluginCommands,
			FToolBarExtensionDelegate::CreateRaw(this, &FSpatialGDKEditorToolbarModule::AddToolbarExtension));
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(ToolbarExtender);
		
	}
}

void FSpatialGDKEditorToolbarModule::AddMenuExtension(FMenuBuilder& Builder)
{
	Builder.BeginSection("SpatialOS Unreal GDK", LOCTEXT("SpatialOSUnrealGDK", "SpatialOS Unreal GDK"));
	{
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StartNative);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StartLocalSpatialDeployment);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StartCloudSpatialDeployment);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction);
#if PLATFORM_WINDOWS
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().OpenCloudDeploymentWindowAction);
#endif
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot);
		Builder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().GenerateTestMaps);
	}
	Builder.EndSection();
}

void FSpatialGDKEditorToolbarModule::AddToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.AddSeparator(NAME_None);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StartNative);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StartLocalSpatialDeployment);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StartCloudSpatialDeployment);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().StopSpatialDeployment);
	Builder.AddComboButton(FUIAction(), FOnGetContent::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateStartDropDownMenuContent),
						   LOCTEXT("StartDropDownMenu_Label", "SpatialOS Network Options"), TAttribute<FText>(),
						   FSlateIcon(FEditorStyle::GetStyleSetName(), "GDK.Start"), true);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().LaunchInspectorWebPageAction);
#if PLATFORM_WINDOWS
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().OpenCloudDeploymentWindowAction);
	Builder.AddComboButton(FUIAction(), FOnGetContent::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateLaunchDeploymentMenuContent),
						   LOCTEXT("GDKDeploymentCombo_Label", "Deployment Tools"), TAttribute<FText>(),
						   FSlateIcon(FEditorStyle::GetStyleSetName(), "GDK.Cloud"), true);
#endif
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchema);
	Builder.AddComboButton(FUIAction(), FOnGetContent::CreateRaw(this, &FSpatialGDKEditorToolbarModule::CreateGenerateSchemaMenuContent),
						   LOCTEXT("GDKSchemaCombo_Label", "Schema Generation Options"), TAttribute<FText>(),
						   FSlateIcon(FEditorStyle::GetStyleSetName(), "GDK.Schema"), true);
	Builder.AddToolBarButton(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSnapshot);
}

TSharedRef<SWidget> FSpatialGDKEditorToolbarModule::CreateGenerateSchemaMenuContent()
{
	FMenuBuilder MenuBuilder(true /*bInShouldCloseWindowAfterMenuSelection*/, PluginCommands);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("GDKSchemaOptionsHeader", "Schema Generation"));
	{
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CreateSpatialGDKSchemaFull);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().DeleteSchemaDatabase);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CleanGenerateSchema);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FSpatialGDKEditorToolbarModule::CreateLaunchDeploymentMenuContent()
{
	FMenuBuilder MenuBuilder(true /*bInShouldCloseWindowAfterMenuSelection*/, PluginCommands);
	MenuBuilder.BeginSection(NAME_None, LOCTEXT("GDKDeploymentOptionsHeader", "Deployment Tools"));
	{
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().OpenLaunchConfigurationEditorAction);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

void OnLocalDeploymentIPChanged(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType != ETextCommit::OnEnter && InCommitType != ETextCommit::OnUserMovedFocus)
	{
		return;
	}

	const FString& InputIpAddress = InText.ToString();
	if (!USpatialGDKEditorSettings::IsValidIP(InputIpAddress))
	{
		FMessageDialog::Open(EAppMsgType::Ok, LOCTEXT("InputValidIPAddress_Prompt", "Please input a valid IP address."));
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Invalid IP address: %s"), *InputIpAddress);
		return;
	}

	USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();
	SpatialGDKEditorSettings->SetExposedRuntimeIP(InputIpAddress);
	UE_LOG(LogSpatialGDKEditorToolbar, Display, TEXT("Setting local deployment IP address to %s"), *InputIpAddress);
}

void OnCloudDeploymentNameChanged(const FText& InText, ETextCommit::Type InCommitType)
{
	if (InCommitType != ETextCommit::OnEnter && InCommitType != ETextCommit::OnUserMovedFocus)
	{
		return;
	}

	const FString& InputDeploymentName = InText.ToString();
	const FRegexPattern DeploymentNamePatternRegex(SpatialConstants::DeploymentPattern);
	FRegexMatcher DeploymentNameRegexMatcher(DeploymentNamePatternRegex, InputDeploymentName);
	if (!InputDeploymentName.IsEmpty() && !DeploymentNameRegexMatcher.FindNext())
	{
		FMessageDialog::Open(EAppMsgType::Ok,
							 FText::Format(LOCTEXT("InputValidDeploymentName_Prompt", "Please input a valid deployment name. {0}"),
										   SpatialConstants::DeploymentPatternHint));
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Invalid deployment name: %s"), *InputDeploymentName);
		return;
	}

	USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();
	SpatialGDKEditorSettings->SetPrimaryDeploymentName(InputDeploymentName);

	UE_LOG(LogSpatialGDKEditorToolbar, Display, TEXT("Setting cloud deployment name to %s"), *InputDeploymentName);
}

TSharedRef<SWidget> FSpatialGDKEditorToolbarModule::CreateStartDropDownMenuContent()
{
	FMenuBuilder MenuBuilder(false /*bInShouldCloseWindowAfterMenuSelection*/, PluginCommands);
	UGeneralProjectSettings* GeneralProjectSettings = GetMutableDefault<UGeneralProjectSettings>();
	USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();
	MenuBuilder.BeginSection("SpatialOSSettings", LOCTEXT("SpatialOSSettings_Label", "SpatialOS Settings"));
	{
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().EnableSpatialNetworking);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("ConnectionFlow", LOCTEXT("ConnectionFlow_Label", "Connection Flow"));
	{
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().LocalDeployment);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().CloudDeployment);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("AdditionalProperties");
	{
		MenuBuilder.AddWidget(
			CreateBetterEditableTextWidget(LOCTEXT("LocalDeploymentIP_Label", "Local Deployment IP: "),
										   FText::FromString(GetDefault<USpatialGDKEditorSettings>()->ExposedRuntimeIP),
										   OnLocalDeploymentIPChanged, FSpatialGDKEditorToolbarModule::IsLocalDeploymentIPEditable),
			FText());

		MenuBuilder.AddWidget(CreateBetterEditableTextWidget(LOCTEXT("CloudDeploymentName_Label", "Cloud Deployment Name: "),
															 FText::FromString(SpatialGDKEditorSettings->GetPrimaryDeploymentName()),
															 OnCloudDeploymentNameChanged,
															 FSpatialGDKEditorToolbarModule::AreCloudDeploymentPropertiesEditable),
							  FText());
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().EnableBuildClientWorker);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().EnableBuildSimulatedPlayer);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("SettingsShortcuts");
	{
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().GDKEditorSettings);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().GDKRuntimeSettings);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("SpatialDebuggerEditorSettings");
	{
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().ToggleSpatialDebuggerEditor);
		MenuBuilder.AddMenuEntry(FSpatialGDKEditorToolbarCommands::Get().ToggleMultiWorkerEditor);
	}
	MenuBuilder.EndSection();

	return MenuBuilder.MakeWidget();
}

TSharedRef<SWidget> FSpatialGDKEditorToolbarModule::CreateBetterEditableTextWidget(const FText& Label, const FText& Text,
																				   FOnTextCommitted::TFuncType OnTextCommitted,
																				   IsEnabledFunc IsEnabled)
{
	return SNew(SHorizontalBox)
		   + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[SNew(STextBlock).Text(Label).IsEnabled_Static(IsEnabled)]
		   + SHorizontalBox::Slot().FillWidth(1.f).VAlign(
			   VAlign_Bottom)[SNew(SEditableTextBox)
								  .OnTextCommitted_Static(OnTextCommitted)
								  .Text(Text)
								  .SelectAllTextWhenFocused(true)
								  .IsEnabled_Static(IsEnabled)
								  .Font(FEditorStyle::GetFontStyle(TEXT("SourceControl.LoginWindow.Font")))];
}

void FSpatialGDKEditorToolbarModule::CreateSnapshotButtonClicked()
{
	OnShowTaskStartNotification("Started snapshot generation");

	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();

	SpatialGDKEditorInstance->GenerateSnapshot(GEditor->GetEditorWorldContext().World(),
											   SpatialGDKEditorSettings->GetSpatialOSSnapshotToSave(),
											   FSimpleDelegate::CreateLambda([this]() {
												   OnShowSuccessNotification("Snapshot successfully generated!");
											   }),
											   FSimpleDelegate::CreateLambda([this]() {
												   OnShowFailedNotification("Snapshot generation failed!");
											   }),
											   FSpatialGDKEditorErrorHandler::CreateLambda([](FString ErrorText) {
												   FMessageDialog::Debugf(FText::FromString(ErrorText));
											   }));
}

void FSpatialGDKEditorToolbarModule::DeleteSchemaDatabaseButtonClicked()
{
	if (FMessageDialog::Open(EAppMsgType::YesNo,
							 LOCTEXT("DeleteSchemaDatabase_Prompt", "Are you sure you want to delete the schema database?"))
		== EAppReturnType::Yes)
	{
		DeleteSchemaDatabase();
	}
}

bool FSpatialGDKEditorToolbarModule::DeleteSchemaDatabase()
{
	OnShowTaskStartNotification(TEXT("Deleting schema database"));
	bool bResult = SpatialGDKEditor::Schema::DeleteSchemaDatabase(SpatialConstants::SCHEMA_DATABASE_FILE_PATH);

	if (bResult)
	{
		OnShowSuccessNotification(TEXT("Schema database deleted"));
	}
	else
	{
		OnShowFailedNotification(TEXT("Failed to delete schema database"));
	}

	return bResult;
}

void FSpatialGDKEditorToolbarModule::CleanSchemaGenerateButtonClicked()
{
	if (FMessageDialog::Open(
			EAppMsgType::YesNo,
			LOCTEXT("DeleteSchemaDatabase_Prompt",
					"Are you sure you want to delete the schema database, delete all generated schema, and regenerate schema?"))
		== EAppReturnType::Yes)
	{
		CleanSchemaGenerate();
	}
}

void FSpatialGDKEditorToolbarModule::CleanSchemaGenerate()
{
	if (DeleteSchemaDatabase())
	{
		SpatialGDKEditor::Schema::ResetSchemaGeneratorStateAndCleanupFolders();
		GenerateSchema(true);
	}
	else
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Failed to delete Schema Database; schema will not be cleaned and regenerated."));
	}
}

void FSpatialGDKEditorToolbarModule::SchemaGenerateButtonClicked()
{
	GenerateSchema(false);
}

void FSpatialGDKEditorToolbarModule::SchemaGenerateFullButtonClicked()
{
	GenerateSchema(true);
}

void FSpatialGDKEditorToolbarModule::HandleGenerateSchemaFailure()
{
	// Run the dialogue on a background task -- this allows the editor UI to update and display Schema Gen errors in the log
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this] {
		if (FMessageDialog::Open(EAppMsgType::YesNo,
								 LOCTEXT("DeleteAndRegenerateSchemaDatabase_Prompt",
										 "Schema generation failed. Common schema generation issues can be solved by deleting all schema "
										 "and generating again. Would you like to clean and retry now?"))
			== EAppReturnType::Yes)
		{
			// GameThread is required for building schema
			AsyncTask(ENamedThreads::GameThread, [this] {
				CleanSchemaGenerate();
			});
		}
	});
}

void FSpatialGDKEditorToolbarModule::OnShowSingleFailureNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [NotificationText] {
		if (FSpatialGDKEditorToolbarModule* Module =
				FModuleManager::GetModulePtr<FSpatialGDKEditorToolbarModule>("SpatialGDKEditorToolbar"))
		{
			Module->ShowSingleFailureNotification(NotificationText);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowSingleFailureNotification(const FString& NotificationText)
{
	// If a task notification already exists then expire it.
	if (TaskNotificationPtr.IsValid())
	{
		TaskNotificationPtr.Pin()->ExpireAndFadeout();
	}

	FNotificationInfo Info(FText::AsCultureInvariant(NotificationText));
	Info.Image = FSpatialGDKEditorToolbarStyle::Get().GetBrush(TEXT("SpatialGDKEditorToolbar.SpatialOSLogo"));
	Info.ExpireDuration = 5.0f;
	Info.bFireAndForget = false;

	TaskNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);
	ShowFailedNotification(NotificationText);
}

void FSpatialGDKEditorToolbarModule::OnShowTaskStartNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [NotificationText] {
		if (FSpatialGDKEditorToolbarModule* Module =
				FModuleManager::GetModulePtr<FSpatialGDKEditorToolbarModule>("SpatialGDKEditorToolbar"))
		{
			Module->ShowTaskStartNotification(NotificationText);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowTaskStartNotification(const FString& NotificationText)
{
	// If a task notification already exists then expire it.
	if (TaskNotificationPtr.IsValid())
	{
		TaskNotificationPtr.Pin()->ExpireAndFadeout();
	}

	if (GEditor && ExecutionStartSound)
	{
		GEditor->PlayEditorSound(ExecutionStartSound);
	}

	FNotificationInfo Info(FText::AsCultureInvariant(NotificationText));
	Info.Image = FSpatialGDKEditorToolbarStyle::Get().GetBrush(TEXT("SpatialGDKEditorToolbar.SpatialOSLogo"));
	Info.ExpireDuration = 5.0f;
	Info.bFireAndForget = false;

	TaskNotificationPtr = FSlateNotificationManager::Get().AddNotification(Info);

	if (TaskNotificationPtr.IsValid())
	{
		TaskNotificationPtr.Pin()->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FSpatialGDKEditorToolbarModule::OnShowSuccessNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [NotificationText] {
		if (FSpatialGDKEditorToolbarModule* Module =
				FModuleManager::GetModulePtr<FSpatialGDKEditorToolbarModule>("SpatialGDKEditorToolbar"))
		{
			Module->ShowSuccessNotification(NotificationText);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowSuccessNotification(const FString& NotificationText)
{
	TSharedPtr<SNotificationItem> Notification = TaskNotificationPtr.Pin();
	if (Notification.IsValid())
	{
		Notification->SetFadeInDuration(0.1f);
		Notification->SetFadeOutDuration(0.5f);
		Notification->SetExpireDuration(5.0f);
		Notification->SetText(FText::AsCultureInvariant(NotificationText));
		Notification->SetCompletionState(SNotificationItem::CS_Success);
		Notification->ExpireAndFadeout();

		if (GEditor && ExecutionSuccessSound)
		{
			GEditor->PlayEditorSound(ExecutionSuccessSound);
		}
	}
}

void FSpatialGDKEditorToolbarModule::OnShowFailedNotification(const FString& NotificationText)
{
	AsyncTask(ENamedThreads::GameThread, [NotificationText] {
		if (FSpatialGDKEditorToolbarModule* Module =
				FModuleManager::GetModulePtr<FSpatialGDKEditorToolbarModule>("SpatialGDKEditorToolbar"))
		{
			Module->ShowFailedNotification(NotificationText);
		}
	});
}

void FSpatialGDKEditorToolbarModule::ShowFailedNotification(const FString& NotificationText)
{
	TSharedPtr<SNotificationItem> Notification = TaskNotificationPtr.Pin();
	if (Notification.IsValid())
	{
		Notification->SetFadeInDuration(0.1f);
		Notification->SetFadeOutDuration(0.5f);
		Notification->SetExpireDuration(5.0);
		Notification->SetText(FText::AsCultureInvariant(NotificationText));
		Notification->SetCompletionState(SNotificationItem::CS_Fail);
		Notification->ExpireAndFadeout();

		if (GEditor && ExecutionFailSound)
		{
			GEditor->PlayEditorSound(ExecutionFailSound);
		}
	}
}

void FSpatialGDKEditorToolbarModule::ToggleSpatialDebuggerEditor()
{
	if (SpatialDebugger.IsValid())
	{
		USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();
		SpatialGDKEditorSettings->SetSpatialDebuggerEditorEnabled(!SpatialGDKEditorSettings->IsSpatialDebuggerEditorEnabled());
		GDK_PROPERTY(Property)* SpatialDebuggerEditorEnabledProperty =
			USpatialGDKEditorSettings::StaticClass()->FindPropertyByName(FName("bSpatialDebuggerEditorEnabled"));
		SpatialGDKEditorSettings->UpdateSinglePropertyInConfigFile(SpatialDebuggerEditorEnabledProperty,
																   SpatialGDKEditorSettings->GetDefaultConfigFilename());

		SpatialDebugger->EditorSpatialToggleDebugger(SpatialGDKEditorSettings->IsSpatialDebuggerEditorEnabled());
	}
	else
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("There was no SpatialDebugger setup when the map was loaded."));
	}
}

void FSpatialGDKEditorToolbarModule::ToggleMultiworkerEditor()
{
	USpatialGDKSettings* SpatialGDKRuntimeSettings = GetMutableDefault<USpatialGDKSettings>();
	SpatialGDKRuntimeSettings->SetMultiWorkerEditorEnabled(!SpatialGDKRuntimeSettings->IsMultiWorkerEditorEnabled());
	GDK_PROPERTY(Property)* EnableMultiWorkerProperty = USpatialGDKSettings::StaticClass()->FindPropertyByName(FName("bEnableMultiWorker"));
	SpatialGDKRuntimeSettings->UpdateSinglePropertyInConfigFile(EnableMultiWorkerProperty,
																SpatialGDKRuntimeSettings->GetDefaultConfigFilename());

	if (SpatialDebugger.IsValid())
	{
		SpatialDebugger->EditorRefreshWorkerRegions();
	}
}

void FSpatialGDKEditorToolbarModule::MapChanged(UWorld* World, EMapChangeType MapChangeType)
{
	if (MapChangeType == EMapChangeType::LoadMap || MapChangeType == EMapChangeType::NewMap)
	{
		// If Spatial networking is enabled then initialize the editor debugging facilities.
		if (GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking())
		{
			InitialiseSpatialDebuggerEditor(World);
		}
	}
	else if (MapChangeType == EMapChangeType::TearDownWorld)
	{
		// Destroy spatial debugger when changing map as it will be invalid
		DestroySpatialDebuggerEditor();
	}
}

bool FSpatialGDKEditorToolbarModule::FetchRuntimeBinaryWrapper(FString RuntimeVersion)
{
	bFetchingRuntimeBinary = true;

	const bool bSuccess = SpatialCommandUtils::FetchRuntimeBinary(RuntimeVersion, GetDefault<USpatialGDKSettings>()->IsRunningInChina());

	if (!bSuccess)
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Could not fetch the local runtime for version %s"), *RuntimeVersion);
		OnShowFailedNotification(TEXT("Failed to fetch local runtime!"));
	}

	bFetchingRuntimeBinary = false;

	return bSuccess;
}

bool FSpatialGDKEditorToolbarModule::FetchInspectorBinaryWrapper(FString InspectorVersion)
{
	bFetchingInspectorBinary = true;

	bool bSuccess = SpatialCommandUtils::FetchInspectorBinary(InspectorVersion, GetDefault<USpatialGDKSettings>()->IsRunningInChina());

	if (!bSuccess)
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Could not fetch the Inspector for version %s"), *InspectorVersion);
		OnShowFailedNotification(TEXT("Failed to fetch local inspector!"));
		bFetchingInspectorBinary = false;
		return false;
	}

#if PLATFORM_MAC
	int32 OutCode = 0;
	FString OutString;
	FString OutErr;
	FString ChmodCommand = FPaths::Combine(SpatialGDKServicesConstants::BinPath, TEXT("chmod"));
	FString ChmodArguments = FString::Printf(TEXT("+x \"%s\""), *SpatialGDKServicesConstants::GetInspectorExecutablePath(InspectorVersion));
	bSuccess = FPlatformProcess::ExecProcess(*ChmodCommand, *ChmodArguments, &OutCode, &OutString, &OutErr);
	if (!bSuccess)
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Could not make the Inspector executable for version %s. %s %s"), *InspectorVersion,
			   *OutString, *OutErr);
		OnShowFailedNotification(TEXT("Failed to fetch local inspector!"));
	}
#endif

	bFetchingInspectorBinary = false;

	return bSuccess;
}

void FSpatialGDKEditorToolbarModule::VerifyAndStartDeployment(FString ForceSnapshot /* = ""*/)
{
	// Don't try and start a local deployment if spatial networking is disabled.
	if (!GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking())
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to start a local deployment but spatial networking is disabled."));
		return;
	}

	if (!IsSnapshotGenerated())
	{
		const USpatialGDKEditorSettings* CurSpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
		if (!SpatialGDKGenerateSnapshot(GEditor->GetEditorWorldContext().World(),
										CurSpatialGDKEditorSettings->GetSpatialOSSnapshotToLoadPath()))
		{
			UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to start a local deployment but failed to generate a snapshot."));
			return;
		}
	}

	// Get the latest launch config.
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();

	FString LaunchConfig;
	if (SpatialGDKEditorSettings->bGenerateDefaultLaunchConfig)
	{
		bool bRedeployRequired = false;
		if (!GenerateAllDefaultWorkerJsons(bRedeployRequired))
		{
			return;
		}
		if (bRedeployRequired)
		{
			LocalDeploymentManager->SetRedeployRequired();
		}

		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		check(EditorWorld);

		LaunchConfig = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()),
									   FString::Printf(TEXT("Improbable/%s_LocalLaunchConfig.json"), *EditorWorld->GetMapName()));

		FSpatialLaunchConfigDescription LaunchConfigDescription = SpatialGDKEditorSettings->LaunchConfigDesc;

		// Force manual connection to true as this is the config for PIE.
		LaunchConfigDescription.ServerWorkerConfiguration.bManualWorkerConnectionOnly = true;
		if (LaunchConfigDescription.ServerWorkerConfiguration.bAutoNumEditorInstances)
		{
			LaunchConfigDescription.ServerWorkerConfiguration.NumEditorInstances = GetWorkerCountFromWorldSettings(*EditorWorld);
		}

		if (!ValidateGeneratedLaunchConfig(LaunchConfigDescription))
		{
			return;
		}

		GenerateLaunchConfig(LaunchConfig, &LaunchConfigDescription, /*bGenerateCloudConfig*/ false);

		// Also create default launch config for cloud deployments.
		{
			// Revert to the setting's flag value for manual connection.
			LaunchConfigDescription.ServerWorkerConfiguration.bManualWorkerConnectionOnly =
				SpatialGDKEditorSettings->LaunchConfigDesc.ServerWorkerConfiguration.bManualWorkerConnectionOnly;
			FString CloudLaunchConfig =
				FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()),
								FString::Printf(TEXT("Improbable/%s_CloudLaunchConfig.json"), *EditorWorld->GetMapName()));
			LaunchConfigDescription.ServerWorkerConfiguration.NumEditorInstances = GetWorkerCountFromWorldSettings(*EditorWorld, true);

			GenerateLaunchConfig(CloudLaunchConfig, &LaunchConfigDescription, /*bGenerateCloudConfig*/ true);
		}
	}
	else
	{
		LaunchConfig = SpatialGDKEditorSettings->GetSpatialOSLaunchConfig();
	}

	const FString LaunchFlags = SpatialGDKEditorSettings->GetSpatialOSCommandLineLaunchFlags();
	const FString SnapshotName = ForceSnapshot.IsEmpty() ? SpatialGDKEditorSettings->GetSpatialOSSnapshotToLoad() : ForceSnapshot;
	const FString SnapshotPath = FPaths::Combine(SpatialGDKServicesConstants::SpatialOSSnapshotFolderPath, SnapshotName);

	const FString RuntimeVersion = SpatialGDKEditorSettings->GetSelectedRuntimeVariantVersion().GetVersionForLocal();

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, LaunchConfig, LaunchFlags, SnapshotPath, RuntimeVersion] {
		if (!FetchRuntimeBinaryWrapper(RuntimeVersion))
		{
			UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to start a local deployment but could not fetch the local runtime."));
			return;
		}

		// If the last local deployment is still stopping then wait until it's finished.
		while (LocalDeploymentManager->IsDeploymentStopping())
		{
			FPlatformProcess::Sleep(0.1f);
		}

		// If schema or worker configurations have been changed then we must restart the deployment.
		if (LocalDeploymentManager->IsRedeployRequired() && LocalDeploymentManager->IsLocalDeploymentRunning())
		{
			UE_LOG(LogSpatialGDKEditorToolbar, Display, TEXT("Local deployment must restart."));
			LocalDeploymentManager->TryStopLocalDeployment();
		}
		else if (LocalDeploymentManager->IsLocalDeploymentRunning())
		{
			// A good local deployment is already running.
			return;
		}

		FLocalDeploymentManager::LocalDeploymentCallback CallBack = [this](bool bSuccess) {
			if (bSuccess)
			{
				StartInspectorProcess(/*OnReady*/ nullptr);
			}
			else
			{
				OnShowFailedNotification(TEXT("Local deployment failed to start"));
			}
		};

		LocalDeploymentManager->TryStartLocalDeployment(LaunchConfig, RuntimeVersion, LaunchFlags, SnapshotPath,
														GetOptionalExposedRuntimeIP(), CallBack);
	});
}

void FSpatialGDKEditorToolbarModule::StartLocalSpatialDeploymentButtonClicked()
{
	VerifyAndStartDeployment();
}

void FSpatialGDKEditorToolbarModule::StopSpatialDeploymentButtonClicked()
{
	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this] {
		const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
		bool bRuntimeShutdown = SpatialGDKEditorSettings->bShutdownRuntimeGracefullyOnPIEExit
									? LocalDeploymentManager->TryStopLocalDeploymentGracefully()
									: LocalDeploymentManager->TryStopLocalDeployment();

		if (!bRuntimeShutdown)
		{
			OnShowFailedNotification(TEXT("Failed to stop local deployment!"));
		}
	});
}

void FSpatialGDKEditorToolbarModule::OpenInspectorURL()
{
	FString WebError;
	FPlatformProcess::LaunchURL(*SpatialGDKServicesConstants::InspectorV2URL, TEXT(""), &WebError);
	if (!WebError.IsEmpty())
	{
		FNotificationInfo Info(FText::FromString(WebError));
		Info.ExpireDuration = 3.0f;
		Info.bUseSuccessFailIcons = true;
		TSharedPtr<SNotificationItem> NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
		NotificationItem->SetCompletionState(SNotificationItem::CS_Fail);
		NotificationItem->ExpireAndFadeout();
	}
}

void FSpatialGDKEditorToolbarModule::StartInspectorProcess(TFunction<void()> OnReady)
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	const FString InspectorVersion = SpatialGDKEditorSettings->GetInspectorVersion();

	AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [this, InspectorVersion, OnReady] {
		if (InspectorProcess && InspectorProcess->Update())
		{
			// We already have an inspector process running. Call ready callback if any.
			if (OnReady)
			{
				OnReady();
			}
			return;
		}

		// Check for any old inspector processes that may be leftover from previous runs. Kill any we find.
		SpatialCommandUtils::TryKillProcessWithName(SpatialGDKServicesConstants::InspectorExe);

		// Grab the inspector binary
		if (!SpatialCommandUtils::FetchInspectorBinary(InspectorVersion, GetDefault<USpatialGDKSettings>()->IsRunningInChina()))
		{
			UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Attempted to fetch the local inspector binary but failed!"));
			OnShowFailedNotification(TEXT("Failed to fetch local inspector!"));
			return;
		}

		FString InspectorArgs = FString::Printf(
			TEXT("--grpc_addr=%s --http_addr=%s --schema_bundle=\"%s\""), *SpatialGDKServicesConstants::InspectorGRPCAddress,
			*SpatialGDKServicesConstants::InspectorHTTPAddress, *SpatialGDKServicesConstants::SchemaBundlePath);

		InspectorProcess = { *SpatialGDKServicesConstants::GetInspectorExecutablePath(InspectorVersion), *InspectorArgs,
							 SpatialGDKServicesConstants::SpatialOSDirectory, /*InHidden*/ true,
							 /*InCreatePipes*/ true };

		FSpatialGDKServicesModule& GDKServices = FModuleManager::GetModuleChecked<FSpatialGDKServicesModule>("SpatialGDKServices");
		TWeakPtr<SSpatialOutputLog> SpatialOutputLog = GDKServices.GetSpatialOutputLog();

		InspectorProcess->OnOutput().BindLambda([this](const FString& Output) {
			UE_LOG(LogSpatialGDKEditorToolbar, Log, TEXT("Inspector: %s"), *Output)
		});

		InspectorProcess->OnCanceled().BindLambda([this] {
			if (InspectorProcess.IsSet() && InspectorProcess->GetReturnCode() != SpatialGDKServicesConstants::ExitCodeSuccess)
			{
				UE_LOG(LogSpatialGDKEditorToolbar, Error, TEXT("Inspector crashed! Please check logs for more details. Exit code: %s"),
					   *FString::FromInt(InspectorProcess->GetReturnCode()));
				OnShowFailedNotification(TEXT("Inspector crashed!"));
			}
		});

		InspectorProcess->Launch();

		if (OnReady)
		{
			OnReady();
		}
	});
}

void FSpatialGDKEditorToolbarModule::LaunchInspectorWebpageButtonClicked()
{
	StartInspectorProcess([this]() {
		OpenInspectorURL();
	});
}

bool FSpatialGDKEditorToolbarModule::StartNativeIsVisible() const
{
	return !GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking();
}

bool FSpatialGDKEditorToolbarModule::StartNativeCanExecute() const
{
	return false;
}

bool FSpatialGDKEditorToolbarModule::StartLocalSpatialDeploymentIsVisible() const
{
	return !LocalDeploymentManager->IsLocalDeploymentRunning() && GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking()
		   && GetDefault<USpatialGDKEditorSettings>()->SpatialOSNetFlowType == ESpatialOSNetFlow::LocalDeployment;
}

bool FSpatialGDKEditorToolbarModule::StartLocalSpatialDeploymentCanExecute() const
{
	return !LocalDeploymentManager->IsDeploymentStarting() && !bFetchingRuntimeBinary;
}

bool FSpatialGDKEditorToolbarModule::StartCloudSpatialDeploymentIsVisible() const
{
	return GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking()
		   && GetDefault<USpatialGDKEditorSettings>()->SpatialOSNetFlowType == ESpatialOSNetFlow::CloudDeployment;
}

bool FSpatialGDKEditorToolbarModule::StartCloudSpatialDeploymentCanExecute() const
{
#if PLATFORM_MAC
	// Launching cloud deployments is not supported on Mac
	// TODO: UNR-3396 - allow launching cloud deployments from mac
	return false;
#else
	return CanBuildAndUpload() && !bStartingCloudDeployment;
#endif
}

bool FSpatialGDKEditorToolbarModule::LaunchInspectorWebpageCanExecute() const
{
	return !bFetchingInspectorBinary;
}

bool FSpatialGDKEditorToolbarModule::StopSpatialDeploymentIsVisible() const
{
	return LocalDeploymentManager->IsLocalDeploymentRunning();
}

bool FSpatialGDKEditorToolbarModule::StopSpatialDeploymentCanExecute() const
{
	return !LocalDeploymentManager->IsDeploymentStopping();
}

void FSpatialGDKEditorToolbarModule::OnToggleSpatialNetworking()
{
	UGeneralProjectSettings* GeneralProjectSettings = GetMutableDefault<UGeneralProjectSettings>();
	GDK_PROPERTY(Property)* SpatialNetworkingProperty =
		UGeneralProjectSettings::StaticClass()->FindPropertyByName(FName("bSpatialNetworking"));

	GeneralProjectSettings->SetUsesSpatialNetworking(!GeneralProjectSettings->UsesSpatialNetworking());
	GeneralProjectSettings->UpdateSinglePropertyInConfigFile(SpatialNetworkingProperty, GeneralProjectSettings->GetDefaultConfigFilename());

	// If Spatial networking is enabled then initialise the SpatialDebugger, otherwise destroy it
	if (GeneralProjectSettings->UsesSpatialNetworking())
	{
		UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
		check(EditorWorld);
		InitialiseSpatialDebuggerEditor(EditorWorld);
	}
	else
	{
		DestroySpatialDebuggerEditor();
	}
}

bool FSpatialGDKEditorToolbarModule::OnIsSpatialNetworkingEnabled() const
{
	return GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking();
}

void FSpatialGDKEditorToolbarModule::GDKEditorSettingsClicked() const
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Editor Settings");
}

void FSpatialGDKEditorToolbarModule::GDKRuntimeSettingsClicked() const
{
	FModuleManager::LoadModuleChecked<ISettingsModule>("Settings").ShowViewer("Project", "SpatialGDKEditor", "Runtime Settings");
}

bool FSpatialGDKEditorToolbarModule::IsLocalDeploymentSelected() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	return SpatialGDKEditorSettings->SpatialOSNetFlowType == ESpatialOSNetFlow::LocalDeployment;
}

bool FSpatialGDKEditorToolbarModule::IsCloudDeploymentSelected() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	return SpatialGDKEditorSettings->SpatialOSNetFlowType == ESpatialOSNetFlow::CloudDeployment;
}

bool FSpatialGDKEditorToolbarModule::IsSpatialOSNetFlowConfigurable() const
{
	return OnIsSpatialNetworkingEnabled() && !(LocalDeploymentManager->IsLocalDeploymentRunning());
}

void FSpatialGDKEditorToolbarModule::LocalDeploymentClicked()
{
	USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();
	SpatialGDKEditorSettings->SetSpatialOSNetFlowType(ESpatialOSNetFlow::LocalDeployment);

	OnAutoStartLocalDeploymentChanged();

	LocalReceptionistProxyServerManager->TryStopReceptionistProxyServer();
}

void FSpatialGDKEditorToolbarModule::CloudDeploymentClicked()
{
	USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();
	SpatialGDKEditorSettings->SetSpatialOSNetFlowType(ESpatialOSNetFlow::CloudDeployment);

	TSharedRef<FSpatialGDKDevAuthTokenGenerator> DevAuthTokenGenerator = SpatialGDKEditorInstance->GetDevAuthTokenGeneratorRef();
	DevAuthTokenGenerator->AsyncGenerateDevAuthToken();

	OnAutoStartLocalDeploymentChanged();
}

bool FSpatialGDKEditorToolbarModule::IsLocalDeploymentIPEditable()
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	return GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking()
		   && (SpatialGDKEditorSettings->SpatialOSNetFlowType == ESpatialOSNetFlow::LocalDeployment);
}

bool FSpatialGDKEditorToolbarModule::AreCloudDeploymentPropertiesEditable()
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	return GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking()
		   && (SpatialGDKEditorSettings->SpatialOSNetFlowType == ESpatialOSNetFlow::CloudDeployment);
}

void FSpatialGDKEditorToolbarModule::OnPropertyChanged(UObject* ObjectBeingModified, FPropertyChangedEvent& PropertyChangedEvent)
{
	if (USpatialGDKEditorSettings* SpatialGDKEditorSettings = Cast<USpatialGDKEditorSettings>(ObjectBeingModified))
	{
		FName PropertyName = PropertyChangedEvent.Property != nullptr ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		FString PropertyNameStr = PropertyName.ToString();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(USpatialGDKEditorSettings, AutoStopLocalDeployment))
		{
			/*
			 * This updates our own local copy of AutoStopLocalDeployment as Settings change.
			 * We keep the copy of the variable as all the USpatialGDKEditorSettings references get
			 * cleaned before all the available callbacks that IModuleInterface exposes. This means that we can't access
			 * this variable through its references after the engine is closed.
			 */
			AutoStopLocalDeployment = SpatialGDKEditorSettings->AutoStopLocalDeployment;
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(USpatialGDKEditorSettings, bAutoStartLocalDeployment))
		{
			OnAutoStartLocalDeploymentChanged();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(USpatialGDKEditorSettings, bConnectServerToCloud))
		{
			LocalReceptionistProxyServerManager->TryStopReceptionistProxyServer();
		}
		else if (PropertyName == GET_MEMBER_NAME_CHECKED(USpatialGDKEditorSettings, bSpatialDebuggerEditorEnabled))
		{
			if (SpatialDebugger.IsValid())
			{
				SpatialDebugger->EditorSpatialToggleDebugger(SpatialGDKEditorSettings->bSpatialDebuggerEditorEnabled);
			}
		}
	}
	if (USpatialGDKSettings* SpatialGDKRuntimeSettings = Cast<USpatialGDKSettings>(ObjectBeingModified))
	{
		FName PropertyName = PropertyChangedEvent.Property != nullptr ? PropertyChangedEvent.Property->GetFName() : NAME_None;
		FString PropertyNameStr = PropertyName.ToString();
		if (PropertyName == GET_MEMBER_NAME_CHECKED(USpatialGDKSettings, bEnableMultiWorker))
		{
			// Update multi-worker settings
			if (SpatialDebugger.IsValid())
			{
				SpatialDebugger->EditorRefreshWorkerRegions();
			}
		}
	}
}

void FSpatialGDKEditorToolbarModule::ShowCloudDeploymentDialog()
{
	// Create and open the cloud configuration dialog
	if (CloudDeploymentSettingsWindowPtr.IsValid())
	{
		CloudDeploymentSettingsWindowPtr->BringToFront();
	}
	else
	{
		CloudDeploymentSettingsWindowPtr = SNew(SWindow)
											   .Title(LOCTEXT("CloudDeploymentConfigurationTitle", "Cloud Deployment Configuration"))
											   .HasCloseButton(true)
											   .SupportsMaximize(false)
											   .SupportsMinimize(false)
											   .SizingRule(ESizingRule::Autosized);

		CloudDeploymentSettingsWindowPtr->SetContent(
			SNew(SBox).WidthOverride(700.0f)[SAssignNew(CloudDeploymentConfigPtr, SSpatialGDKCloudDeploymentConfiguration)
												 .SpatialGDKEditor(SpatialGDKEditorInstance)
												 .ParentWindow(CloudDeploymentSettingsWindowPtr)]);
		CloudDeploymentSettingsWindowPtr->SetOnWindowClosed(FOnWindowClosed::CreateLambda([=](const TSharedRef<SWindow>& WindowArg) {
			CloudDeploymentSettingsWindowPtr = nullptr;
		}));
		FSlateApplication::Get().AddWindow(CloudDeploymentSettingsWindowPtr.ToSharedRef());
	}
}

void FSpatialGDKEditorToolbarModule::OpenLaunchConfigurationEditor()
{
	ULaunchConfigurationEditor::OpenModalWindow(nullptr);
}

void FSpatialGDKEditorToolbarModule::LaunchOrShowCloudDeployment()
{
	if (CanStartCloudDeployment())
	{
		OnStartCloudDeployment();
	}
	else
	{
		ShowCloudDeploymentDialog();
	}
}

void FSpatialGDKEditorToolbarModule::GenerateSchema(bool bFullScan)
{
	LocalDeploymentManager->SetRedeployRequired();

	const bool bFullScanRequired = SpatialGDKEditorInstance->FullScanRequired();

	FSpatialGDKEditor::ESchemaGenerationMethod GenerationMethod;
	FString OnTaskStartMessage;
	FString OnTaskCompleteMessage;
	FString OnTaskFailMessage;
	if (bFullScanRequired || bFullScan)
	{
		GenerationMethod = FSpatialGDKEditor::FullAssetScan;
		const TCHAR* RequiredStr = bFullScanRequired ? TEXT(" required") : TEXT("");
		OnTaskStartMessage = FString::Printf(TEXT("Generating schema (full scan%s)"), RequiredStr);
		OnTaskCompleteMessage = TEXT("Full schema generation complete");
		OnTaskFailMessage = TEXT("Full schema generation failed");
	}
	else
	{
		GenerationMethod = FSpatialGDKEditor::InMemoryAsset;
		OnTaskStartMessage = TEXT("Generating schema (incremental)");
		OnTaskCompleteMessage = TEXT("Incremental schema generation completed!");
		OnTaskFailMessage = TEXT("Incremental schema generation failed");
	}

	OnShowTaskStartNotification(OnTaskStartMessage);
	SpatialGDKEditorInstance->GenerateSchema(GenerationMethod, [this, OnTaskCompleteMessage = MoveTemp(OnTaskCompleteMessage),
																OnTaskFailMessage = MoveTemp(OnTaskFailMessage)](bool bResult) {
		if (bResult)
		{
			OnShowSuccessNotification(OnTaskCompleteMessage);
		}
		else
		{
			OnShowFailedNotification(OnTaskFailMessage);

			HandleGenerateSchemaFailure();
		}
	});
	;
}

bool FSpatialGDKEditorToolbarModule::IsSnapshotGenerated() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	return FPaths::FileExists(SpatialGDKEditorSettings->GetSpatialOSSnapshotToLoadPath());
}

FString FSpatialGDKEditorToolbarModule::GetOptionalExposedRuntimeIP() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	if (SpatialGDKEditorSettings->SpatialOSNetFlowType == ESpatialOSNetFlow::LocalDeployment)
	{
		return SpatialGDKEditorSettings->ExposedRuntimeIP;
	}
	else
	{
		return TEXT("");
	}
}

void FSpatialGDKEditorToolbarModule::OnAutoStartLocalDeploymentChanged()
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();

	// Only auto start local deployment when the setting is checked AND local deployment connection flow is selected.
	bool bShouldAutoStartLocalDeployment = (SpatialGDKEditorSettings->bAutoStartLocalDeployment
											&& SpatialGDKEditorSettings->SpatialOSNetFlowType == ESpatialOSNetFlow::LocalDeployment);

	// TODO: UNR-1776 Workaround for SpatialNetDriver requiring editor settings.
	LocalDeploymentManager->SetAutoDeploy(bShouldAutoStartLocalDeployment);

	if (bShouldAutoStartLocalDeployment)
	{
		if (!UEditorEngine::TryStartSpatialDeployment.IsBound())
		{
			// Bind the TryStartSpatialDeployment delegate if autostart is enabled.
			UEditorEngine::TryStartSpatialDeployment.BindLambda([this](FString ForceSnapshot) {
				if (GetDefault<USpatialGDKEditorSettings>()->bAutoStartLocalDeployment
					&& GetDefault<UGeneralProjectSettings>()->UsesSpatialNetworking())
				{
					VerifyAndStartDeployment(ForceSnapshot);
				}
			});
		}
	}
	else
	{
		if (UEditorEngine::TryStartSpatialDeployment.IsBound())
		{
			// Unbind the TryStartSpatialDeployment if autostart is disabled.
			UEditorEngine::TryStartSpatialDeployment.Unbind();
		}
	}
}

void FSpatialGDKEditorToolbarModule::GenerateCloudConfigFromCurrentMap()
{
	USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();

	UWorld* EditorWorld = GEditor->GetEditorWorldContext().World();
	check(EditorWorld != nullptr);

	const FString LaunchConfig = FPaths::Combine(FPaths::ConvertRelativePathToFull(FPaths::ProjectIntermediateDir()),
												 FString::Printf(TEXT("Improbable/%s_CloudLaunchConfig.json"), *EditorWorld->GetMapName()));

	FSpatialLaunchConfigDescription LaunchConfiguration = SpatialGDKEditorSettings->LaunchConfigDesc;

	LaunchConfiguration.ServerWorkerConfiguration.NumEditorInstances = GetWorkerCountFromWorldSettings(*EditorWorld, true);

	GenerateLaunchConfig(LaunchConfig, &LaunchConfiguration, /*bGenerateCloudConfig*/ true);

	SpatialGDKEditorSettings->SetPrimaryLaunchConfigPath(LaunchConfig);
}

FReply FSpatialGDKEditorToolbarModule::OnStartCloudDeployment()
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();

	if (!SpatialGDKEditorSettings->IsDeploymentConfigurationValid())
	{
		OnShowFailedNotification(TEXT("Deployment configuration is not valid."));

		return FReply::Unhandled();
	}

	if (SpatialGDKEditorSettings->ShouldAutoGenerateCloudLaunchConfig())
	{
		GenerateCloudConfigFromCurrentMap();
	}

	if (!SpatialGDKEditorSettings->CheckManualWorkerConnectionOnLaunch())
	{
		OnShowFailedNotification(TEXT("Launch halted because of unexpected workers requiring manual launch."));

		return FReply::Unhandled();
	}

	AddDeploymentTagIfMissing(SpatialConstants::DEV_LOGIN_TAG);

	CloudDeploymentConfiguration.InitFromSettings();

	const FString& DeploymentName = CloudDeploymentConfiguration.PrimaryDeploymentName;
	UE_LOG(LogSpatialGDKEditorToolbar, Display, TEXT("Setting deployment to connect to %s"), *DeploymentName);

	if (CloudDeploymentConfiguration.bBuildAndUploadAssembly)
	{
		if (CloudDeploymentConfiguration.bGenerateSchema)
		{
			if (SpatialGDKEditorInstance->FullScanRequired())
			{
				FMessageDialog::Open(EAppMsgType::Ok,
									 LOCTEXT("FullSchemaGenRequired_Prompt",
											 "A full schema generation is required at least once before you can start a cloud deployment. "
											 "Press the Schema button before starting a cloud deployment."));
				OnShowSingleFailureNotification(TEXT("Generate schema failed."));
				return FReply::Unhandled();
			}

			bool bHasResult{ false };
			bool bResult{ false };
			SpatialGDKEditorInstance->GenerateSchema(FSpatialGDKEditor::InMemoryAsset, [&bHasResult, &bResult](bool bTaskResult) {
				bResult = bTaskResult;
				bHasResult = true;
			});
			checkf(bHasResult, TEXT("Result is expected to be returned synchronously."));
			if (!bResult)
			{
				OnShowSingleFailureNotification(TEXT("Generate schema failed."));
				return FReply::Unhandled();
			}
		}

		if (CloudDeploymentConfiguration.bGenerateSnapshot)
		{
			if (!SpatialGDKGenerateSnapshot(GEditor->GetEditorWorldContext().World(), CloudDeploymentConfiguration.SnapshotPath))
			{
				OnShowSingleFailureNotification(TEXT("Generate snapshot failed."));
				return FReply::Unhandled();
			}
		}

 
		FGlobalTabmanager::Get()->TryInvokeTab(FName(TEXT("OutputLog")));
 
		TSharedRef<FSpatialGDKPackageAssembly> PackageAssembly = SpatialGDKEditorInstance->GetPackageAssemblyRef();
		PackageAssembly->OnSuccess.BindRaw(this, &FSpatialGDKEditorToolbarModule::OnBuildSuccess);
		PackageAssembly->BuildAndUploadAssembly(CloudDeploymentConfiguration);
	}
	else
	{
		UE_LOG(LogSpatialGDKEditorToolbar, Display, TEXT("Skipping building and uploading assembly."));
		OnBuildSuccess();
	}

	return FReply::Handled();
}

void FSpatialGDKEditorToolbarModule::OnBuildSuccess()
{
	bStartingCloudDeployment = true;

	auto StartCloudDeployment = [this]() {
		OnShowTaskStartNotification(
			FString::Printf(TEXT("Starting cloud deployment: %s"), *CloudDeploymentConfiguration.PrimaryDeploymentName));
		SpatialGDKEditorInstance->StartCloudDeployment(
			CloudDeploymentConfiguration, FSimpleDelegate::CreateLambda([this]() {
				OnStartCloudDeploymentFinished();
				OnShowSuccessNotification("Successfully started cloud deployment.");
			}),
			FSimpleDelegate::CreateLambda([this]() {
				OnStartCloudDeploymentFinished();
				OnShowFailedNotification("Failed to start cloud deployment. See output logs for details.");
			}));
	};

	AttemptSpatialAuthResult = Async(
		EAsyncExecution::Thread,
		[]() {
			return SpatialCommandUtils::AttemptSpatialAuth(GetDefault<USpatialGDKSettings>()->IsRunningInChina());
		},
		[this, StartCloudDeployment]() {
			if (AttemptSpatialAuthResult.IsReady() && AttemptSpatialAuthResult.Get() == true)
			{
				StartCloudDeployment();
			}
			else
			{
				OnStartCloudDeploymentFinished();
				OnShowFailedNotification(TEXT("Failed to launch cloud deployment. Unable to authenticate with SpatialOS."));
			}
		});
}

void FSpatialGDKEditorToolbarModule::OnStartCloudDeploymentFinished()
{
	AsyncTask(ENamedThreads::GameThread, [this] {
		bStartingCloudDeployment = false;
	});
}

bool FSpatialGDKEditorToolbarModule::IsDeploymentConfigurationValid() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	return !FSpatialGDKServicesModule::GetProjectName().IsEmpty() && !SpatialGDKEditorSettings->GetPrimaryDeploymentName().IsEmpty()
		   && !SpatialGDKEditorSettings->GetAssemblyName().IsEmpty() && !SpatialGDKEditorSettings->GetSnapshotPath().IsEmpty()
		   && (!SpatialGDKEditorSettings->GetPrimaryLaunchConfigPath().IsEmpty()
			   || SpatialGDKEditorSettings->ShouldAutoGenerateCloudLaunchConfig());
}

bool FSpatialGDKEditorToolbarModule::CanBuildAndUpload() const
{
	return SpatialGDKEditorInstance->GetPackageAssemblyRef()->CanBuild();
}

bool FSpatialGDKEditorToolbarModule::CanStartCloudDeployment() const
{
	return IsDeploymentConfigurationValid() && CanBuildAndUpload() && !bStartingCloudDeployment;
}

bool FSpatialGDKEditorToolbarModule::IsSimulatedPlayersEnabled() const
{
	return GetDefault<USpatialGDKEditorSettings>()->IsSimulatedPlayersEnabled();
}

void FSpatialGDKEditorToolbarModule::OnCheckedSimulatedPlayers()
{
	GetMutableDefault<USpatialGDKEditorSettings>()->SetSimulatedPlayersEnabledState(!IsSimulatedPlayersEnabled());
}

bool FSpatialGDKEditorToolbarModule::IsBuildClientWorkerEnabled() const
{
	return GetDefault<USpatialGDKEditorSettings>()->IsBuildClientWorkerEnabled();
}

void FSpatialGDKEditorToolbarModule::DestroySpatialDebuggerEditor()
{
	if (SpatialDebugger.IsValid())
	{
		SpatialDebugger->Destroy();
		SpatialDebugger = nullptr;
		ASpatialDebugger::EditorRefreshDisplay();
	}
}

void FSpatialGDKEditorToolbarModule::InitialiseSpatialDebuggerEditor(UWorld* World)
{
	const USpatialGDKSettings* SpatialGDKRuntimeSettings = GetDefault<USpatialGDKSettings>();

	if (SpatialGDKRuntimeSettings->SpatialDebugger != nullptr)
	{
		// If spatial debugger set then create the SpatialDebugger for this map to be used in the editor
		FActorSpawnParameters SpawnParameters;
		SpawnParameters.bHideFromSceneOutliner = true;
		SpatialDebugger = World->SpawnActor<ASpatialDebugger>(SpatialGDKRuntimeSettings->SpatialDebugger, SpawnParameters);
		const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
		SpatialDebugger->EditorSpatialToggleDebugger(SpatialGDKEditorSettings->bSpatialDebuggerEditorEnabled);
	}
}

bool FSpatialGDKEditorToolbarModule::IsSpatialDebuggerEditorEnabled() const
{
	const USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetDefault<USpatialGDKEditorSettings>();
	return AllowWorkerBoundaries() && SpatialGDKEditorSettings->bSpatialDebuggerEditorEnabled;
}

bool FSpatialGDKEditorToolbarModule::IsMultiWorkerEnabled() const
{
	const USpatialGDKSettings* SpatialGDKRuntimeSettings = GetDefault<USpatialGDKSettings>();
	return SpatialGDKRuntimeSettings->bEnableMultiWorker;
}

bool FSpatialGDKEditorToolbarModule::AllowWorkerBoundaries() const
{
	return SpatialDebugger.IsValid() && SpatialDebugger->EditorAllowWorkerBoundaries();
}

void FSpatialGDKEditorToolbarModule::OnCheckedBuildClientWorker()
{
	GetMutableDefault<USpatialGDKEditorSettings>()->SetBuildClientWorker(!IsBuildClientWorkerEnabled());
}

void FSpatialGDKEditorToolbarModule::AddDeploymentTagIfMissing(const FString& TagToAdd)
{
	if (TagToAdd.IsEmpty())
	{
		return;
	}

	USpatialGDKEditorSettings* SpatialGDKEditorSettings = GetMutableDefault<USpatialGDKEditorSettings>();

	FString Tags = SpatialGDKEditorSettings->GetDeploymentTags();
	TArray<FString> ExistingTags;
	Tags.ParseIntoArray(ExistingTags, TEXT(" "));

	if (!ExistingTags.Contains(TagToAdd))
	{
		if (ExistingTags.Num() > 0)
		{
			Tags += TEXT(" ");
		}

		Tags += TagToAdd;
		SpatialGDKEditorSettings->SetDeploymentTags(Tags);
	}
}

void FSpatialGDKEditorToolbarModule::GenerateTestMaps()
{
	OnShowTaskStartNotification(TEXT("Generating test maps"));
	if (SpatialGDK::TestMapGeneration::GenerateTestMaps())
	{
		OnShowSuccessNotification(TEXT("Successfully generated test maps!"));
	}
	else
	{
		OnShowFailedNotification(TEXT("Failed to generate test maps. See output log for details."));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FSpatialGDKEditorToolbarModule, SpatialGDKEditorToolbar)
