/*
RWIO 2022 batch DFF tools (optional).
Requires rwio2022.dli to be installed and loaded.
This startup script defines the entry points called by RWIO's batch-menu items.
It does not modify the scene until a user clicks Import/Export.
*/
global RwioDffBatchOpenImportDialog
global RwioDffBatchOpenExportDialog

fn RwioDffBatchOpenImportDialog ignored = (
    rollout rwioBatchImporter "RWIO 2022 Batch DFF Import" width:340 height:120 (
        label message "Imports all .dff files in a chosen folder." pos:[14,12] width:310
        checkbox quiet "Skip per-file import dialogs" checked:true pos:[14,39] width:300
        button runImport "Choose Folder && Import" pos:[14,72] width:310 height:30
        on runImport pressed do (
            local folder = getSavePath caption:"RWIO - Choose DFF folder to import"
            if folder != undefined do (
                local files = getFiles (folder + "\\*.dff")
                local success = 0
                local failed = 0
                for file in files do (
                    local worked = false
                    try (
                        if quiet.checked then
                            importFile file #noPrompt
                        else
                            importFile file
                        worked = true
                    ) catch (
                        format "RWIO batch import failed: % (%)\n" file (getCurrentException())
                    )
                    if worked then success += 1 else failed += 1
                )
                messageBox ((success as string) + " imported; " + (failed as string) + " failed.") title:"RWIO Batch Import"
            )
        )
    )
    createDialog rwioBatchImporter modal:false
)

fn RwioDffBatchOpenExportDialog ignored = (
    rollout rwioBatchExporter "RWIO 2022 Batch DFF Export" width:340 height:135 (
        label message "Exports selected top-level objects one DFF at a time." pos:[14,12] width:315
        label hint "Names: rwio_0001.dff, rwio_0002.dff, ..." pos:[14,33] width:315
        button runExport "Choose Folder && Export Selection" pos:[14,72] width:310 height:30
        on runExport pressed do (
            local destination = getSavePath caption:"RWIO - Choose DFF export folder"
            if destination != undefined do (
                local originalSelection = selection as array
                local success = 0
                local failed = 0
                if originalSelection.count == 0 then (
                    messageBox "Select one or more objects before running batch export." title:"RWIO Batch Export"
                ) else (
                    try (
                        for i = 1 to originalSelection.count do (
                            local root = originalSelection[i]
                            local worked = false
                            try (
                                select root
                                local filename = destination + "\\rwio_" + (formattedPrint i format:"04d") + ".dff"
                                exportFile filename #noPrompt selectedOnly:true
                                worked = true
                            ) catch (
                                format "RWIO batch export failed for %: %\n" root.name (getCurrentException())
                            )
                            if worked then success += 1 else failed += 1
                        )
                    ) catch (
                        format "RWIO batch export interrupted: %\n" (getCurrentException())
                    )
                    try (select originalSelection) catch ()
                    messageBox ((success as string) + " exported; " + (failed as string) + " failed.") title:"RWIO Batch Export"
                )
            )
        )
    )
    createDialog rwioBatchExporter modal:false
)
