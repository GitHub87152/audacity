/**********************************************************************

Audacity: A Digital Audio Editor

ProjectManager.cpp

Paul Licameli split from AudacityProject.cpp

**********************************************************************/

#include "ProjectManager.h"

#include "Experimental.h"

#include "AdornedRulerPanel.h"
#include "AudioIO.h"
#include "AutoRecovery.h"
#include "BlockFile.h"
#include "Clipboard.h"
#include "DirManager.h"
#include "FileNames.h"
#include "Menus.h"
#include "MissingAliasFileDialog.h"
#include "ModuleManager.h"
#include "Project.h"
#include "ProjectAudioIO.h"
#include "ProjectAudioManager.h"
#include "ProjectFileIO.h"
#include "ProjectFileManager.h"
#include "ProjectHistory.h"
#include "ProjectSelectionManager.h"
#include "ProjectSettings.h"
#include "ProjectWindow.h"
#include "SelectUtilities.h"
#include "TrackPanel.h"
#include "TrackUtilities.h"
#include "UndoManager.h"
#include "WaveTrack.h"
#include "wxFileNameWrapper.h"
#include "import/ImportMIDI.h"
#include "ondemand/ODManager.h"
#include "prefs/QualityPrefs.h"
#include "toolbars/ControlToolBar.h"
#include "toolbars/MixerToolBar.h"
#include "toolbars/SelectionBar.h"
#include "toolbars/SpectralSelectionBar.h"
#include "toolbars/ToolManager.h"
#include "widgets/AudacityMessageBox.h"
#include "widgets/FileHistory.h"
#include "widgets/ErrorDialog.h"

#include <wx/dataobj.h>
#include <wx/dnd.h>

const int AudacityProjectTimerID = 5200;

static AudacityProject::AttachedObjects::RegisteredFactory sProjectManagerKey {
   []( AudacityProject &project ) {
      return std::make_shared< ProjectManager >( project );
   }
};

ProjectManager &ProjectManager::Get( AudacityProject &project )
{
   return project.AttachedObjects::Get< ProjectManager >( sProjectManagerKey );
}

const ProjectManager &ProjectManager::Get( const AudacityProject &project )
{
   return Get( const_cast< AudacityProject & >( project ) );
}

ProjectManager::ProjectManager( AudacityProject &project )
   : mProject{ project }
   , mTimer{ std::make_unique<wxTimer>(this, AudacityProjectTimerID) }
{
   auto &window = ProjectWindow::Get( mProject );
   window.Bind( wxEVT_CLOSE_WINDOW, &ProjectManager::OnCloseWindow, this );
   mProject.Bind(EVT_PROJECT_STATUS_UPDATE,
      &ProjectManager::OnStatusChange, this);
}

ProjectManager::~ProjectManager() = default;

// PRL:  This event type definition used to be in AudacityApp.h, which created
// a bad compilation dependency.  The event was never emitted anywhere.  I
// preserve it and its handler here but I move it to remove the dependency.
// Asynchronous open
wxDECLARE_EXPORTED_EVENT(AUDACITY_DLL_API,
                         EVT_OPEN_AUDIO_FILE, wxCommandEvent);
wxDEFINE_EVENT(EVT_OPEN_AUDIO_FILE, wxCommandEvent);

BEGIN_EVENT_TABLE( ProjectManager, wxEvtHandler )
   EVT_COMMAND(wxID_ANY, EVT_OPEN_AUDIO_FILE, ProjectManager::OnOpenAudioFile)
   EVT_TIMER(AudacityProjectTimerID, ProjectManager::OnTimer)
END_EVENT_TABLE()

bool ProjectManager::sbWindowRectAlreadySaved = false;

void ProjectManager::SaveWindowSize()
{
   if (sbWindowRectAlreadySaved)
   {
      return;
   }
   bool validWindowForSaveWindowSize = FALSE;
   ProjectWindow * validProject = nullptr;
   bool foundIconizedProject = FALSE;
   for ( auto pProject : AllProjects{} )
   {
      auto &window = ProjectWindow::Get( *pProject );
      if (!window.IsIconized()) {
         validWindowForSaveWindowSize = TRUE;
         validProject = &window;
         break;
      }
      else
         foundIconizedProject =  TRUE;

   }
   if (validWindowForSaveWindowSize)
   {
      wxRect windowRect = validProject->GetRect();
      wxRect normalRect = validProject->GetNormalizedWindowState();
      bool wndMaximized = validProject->IsMaximized();
      gPrefs->Write(wxT("/Window/X"), windowRect.GetX());
      gPrefs->Write(wxT("/Window/Y"), windowRect.GetY());
      gPrefs->Write(wxT("/Window/Width"), windowRect.GetWidth());
      gPrefs->Write(wxT("/Window/Height"), windowRect.GetHeight());
      gPrefs->Write(wxT("/Window/Maximized"), wndMaximized);
      gPrefs->Write(wxT("/Window/Normal_X"), normalRect.GetX());
      gPrefs->Write(wxT("/Window/Normal_Y"), normalRect.GetY());
      gPrefs->Write(wxT("/Window/Normal_Width"), normalRect.GetWidth());
      gPrefs->Write(wxT("/Window/Normal_Height"), normalRect.GetHeight());
      gPrefs->Write(wxT("/Window/Iconized"), FALSE);
   }
   else
   {
      if (foundIconizedProject) {
         validProject = &ProjectWindow::Get( **AllProjects{}.begin() );
         bool wndMaximized = validProject->IsMaximized();
         wxRect normalRect = validProject->GetNormalizedWindowState();
         // store only the normal rectangle because the itemized rectangle
         // makes no sense for an opening project window
         gPrefs->Write(wxT("/Window/X"), normalRect.GetX());
         gPrefs->Write(wxT("/Window/Y"), normalRect.GetY());
         gPrefs->Write(wxT("/Window/Width"), normalRect.GetWidth());
         gPrefs->Write(wxT("/Window/Height"), normalRect.GetHeight());
         gPrefs->Write(wxT("/Window/Maximized"), wndMaximized);
         gPrefs->Write(wxT("/Window/Normal_X"), normalRect.GetX());
         gPrefs->Write(wxT("/Window/Normal_Y"), normalRect.GetY());
         gPrefs->Write(wxT("/Window/Normal_Width"), normalRect.GetWidth());
         gPrefs->Write(wxT("/Window/Normal_Height"), normalRect.GetHeight());
         gPrefs->Write(wxT("/Window/Iconized"), TRUE);
      }
      else {
         // this would be a very strange case that might possibly occur on the Mac
         // Audacity would have to be running with no projects open
         // in this case we are going to write only the default values
         wxRect defWndRect;
         GetDefaultWindowRect(&defWndRect);
         gPrefs->Write(wxT("/Window/X"), defWndRect.GetX());
         gPrefs->Write(wxT("/Window/Y"), defWndRect.GetY());
         gPrefs->Write(wxT("/Window/Width"), defWndRect.GetWidth());
         gPrefs->Write(wxT("/Window/Height"), defWndRect.GetHeight());
         gPrefs->Write(wxT("/Window/Maximized"), FALSE);
         gPrefs->Write(wxT("/Window/Normal_X"), defWndRect.GetX());
         gPrefs->Write(wxT("/Window/Normal_Y"), defWndRect.GetY());
         gPrefs->Write(wxT("/Window/Normal_Width"), defWndRect.GetWidth());
         gPrefs->Write(wxT("/Window/Normal_Height"), defWndRect.GetHeight());
         gPrefs->Write(wxT("/Window/Iconized"), FALSE);
      }
   }
   gPrefs->Flush();
   sbWindowRectAlreadySaved = true;
}

#if wxUSE_DRAG_AND_DROP
class FileObject final : public wxFileDataObject
{
public:
   FileObject()
   {
   }

   bool IsSupportedFormat(const wxDataFormat & format, Direction WXUNUSED(dir = Get)) const
      // PRL:  This function does NOT override any inherited virtual!  What does it do?
   {
      if (format.GetType() == wxDF_FILENAME) {
         return true;
      }

#if defined(__WXMAC__)
#if !wxCHECK_VERSION(3, 0, 0)
      if (format.GetFormatId() == kDragPromisedFlavorFindFile) {
         return true;
      }
#endif
#endif

      return false;
   }
};

class DropTarget final : public wxFileDropTarget
{
public:
   DropTarget(AudacityProject *proj)
   {
      mProject = proj;

      // SetDataObject takes ownership
      SetDataObject(safenew FileObject());
   }

   ~DropTarget()
   {
   }

#if defined(__WXMAC__)
#if !wxCHECK_VERSION(3, 0, 0)
   bool GetData() override
   {
      bool foundSupported = false;
      bool firstFileAdded = false;
      OSErr result;

      UInt16 items = 0;
      CountDragItems((DragReference)m_currentDrag, &items);

      for (UInt16 index = 1; index <= items; index++) {

         DragItemRef theItem = 0;
         GetDragItemReferenceNumber((DragReference)m_currentDrag, index, &theItem);

         UInt16 flavors = 0;
         CountDragItemFlavors((DragReference)m_currentDrag, theItem , &flavors ) ;

         for (UInt16 flavor = 1 ;flavor <= flavors; flavor++) {

            FlavorType theType = 0;
            result = GetFlavorType((DragReference)m_currentDrag, theItem, flavor, &theType);
            if (theType != kDragPromisedFlavorFindFile && theType != kDragFlavorTypeHFS) {
               continue;
            }
            foundSupported = true;

            Size dataSize = 0;
            GetFlavorDataSize((DragReference)m_currentDrag, theItem, theType, &dataSize);

            ArrayOf<char> theData{ dataSize };
            GetFlavorData((DragReference)m_currentDrag, theItem, theType, (void*) theData.get(), &dataSize, 0L);

            wxString name;
            if (theType == kDragPromisedFlavorFindFile) {
               name = wxMacFSSpec2MacFilename((FSSpec *)theData.get());
            }
            else if (theType == kDragFlavorTypeHFS) {
               name = wxMacFSSpec2MacFilename(&((HFSFlavor *)theData.get())->fileSpec);
            }

            if (!firstFileAdded) {
               // reset file list
               ((wxFileDataObject*)GetDataObject())->SetData(0, "");
               firstFileAdded = true;
            }

            ((wxFileDataObject*)GetDataObject())->AddFile(name);

            // We only want to process one flavor
            break;
         }
      }
      return foundSupported;
   }
#endif

   bool OnDrop(wxCoord x, wxCoord y) override
   {
      // bool foundSupported = false;
#if !wxCHECK_VERSION(3, 0, 0)
      bool firstFileAdded = false;
      OSErr result;

      UInt16 items = 0;
      CountDragItems((DragReference)m_currentDrag, &items);

      for (UInt16 index = 1; index <= items; index++) {

         DragItemRef theItem = 0;
         GetDragItemReferenceNumber((DragReference)m_currentDrag, index, &theItem);

         UInt16 flavors = 0;
         CountDragItemFlavors((DragReference)m_currentDrag, theItem , &flavors ) ;

         for (UInt16 flavor = 1 ;flavor <= flavors; flavor++) {

            FlavorType theType = 0;
            result = GetFlavorType((DragReference)m_currentDrag, theItem, flavor, &theType);
            if (theType != kDragPromisedFlavorFindFile && theType != kDragFlavorTypeHFS) {
               continue;
            }
            return true;
         }
      }
#endif
      return CurrentDragHasSupportedFormat();
   }

#endif

   bool OnDropFiles(wxCoord WXUNUSED(x), wxCoord WXUNUSED(y), const wxArrayString& filenames) override
   {
      // Experiment shows that this function can be reached while there is no
      // catch block above in wxWidgets.  So stop all exceptions here.
      return GuardedCall< bool > ( [&] {
         //sort by OD non OD.  load Non OD first so user can start editing asap.
         wxArrayString sortednames(filenames);
         sortednames.Sort(CompareNoCaseFileName);

         ODManager::Pauser pauser;

         auto cleanup = finally( [&] {
            ProjectWindow::Get( *mProject ).HandleResize(); // Adjust scrollers for NEW track sizes.
         } );

         for (const auto &name : sortednames) {
#ifdef USE_MIDI
            if (FileNames::IsMidi(name))
               DoImportMIDI( *mProject, name );
            else
#endif
               ProjectFileManager::Get( *mProject ).Import(name);
         }

         auto &window = ProjectWindow::Get( *mProject );
         window.ZoomAfterImport(nullptr);

         return true;
      } );
   }

private:
   AudacityProject *mProject;
};

#endif

AudacityProject *ProjectManager::New()
{
   wxRect wndRect;
   bool bMaximized = false;
   bool bIconized = false;
   GetNextWindowPlacement(&wndRect, &bMaximized, &bIconized);
   
   // Create and show a NEW project
   // Use a non-default deleter in the smart pointer!
   auto sp = std::make_shared< AudacityProject >();
   AllProjects{}.Add( sp );
   auto p = sp.get();
   auto &project = *p;
   auto &projectHistory = ProjectHistory::Get( project );
   auto &projectManager = Get( project );
   auto &window = ProjectWindow::Get( *p );
   window.Init();

   ProjectFileIO::Get( *p ).SetProjectTitle();
   
   MissingAliasFilesDialog::SetShouldShow(true);
   MenuManager::Get( project ).CreateMenusAndCommands( project );
   
   projectHistory.InitialState();
   projectManager.RestartTimer();
   
   // wxGTK3 seems to need to require creating the window using default position
   // and then manually positioning it.
   window.SetPosition(wndRect.GetPosition());
   
   if(bMaximized) {
      window.Maximize(true);
   }
   else if (bIconized) {
      // if the user close down and iconized state we could start back up and iconized state
      // window.Iconize(TRUE);
   }
   
   //Initialise the Listeners
   auto gAudioIO = AudioIO::Get();
   gAudioIO->SetListener(
      ProjectAudioManager::Get( project ).shared_from_this() );
   auto &projectSelectionManager = ProjectSelectionManager::Get( project );
   SelectionBar::Get( project ).SetListener( &projectSelectionManager );
#ifdef EXPERIMENTAL_SPECTRAL_EDITING
   SpectralSelectionBar::Get( project ).SetListener( &projectSelectionManager );
#endif
   
#if wxUSE_DRAG_AND_DROP
   // We can import now, so become a drag target
   //   SetDropTarget(safenew AudacityDropTarget(this));
   //   mTrackPanel->SetDropTarget(safenew AudacityDropTarget(this));
   
   // SetDropTarget takes ownership
   TrackPanel::Get( project ).SetDropTarget( safenew DropTarget( &project ) );
#endif
   
   //Set the NEW project as active:
   SetActiveProject(p);
   
   // Okay, GetActiveProject() is ready. Now we can get its CommandManager,
   // and add the shortcut keys to the tooltips.
   ToolManager::Get( *p ).RegenerateTooltips();
   
   ModuleManager::Get().Dispatch(ProjectInitialized);
   
   window.Show(true);
   
   return p;
}

// LL: All objects that have a reference to the DirManager should
//     be deleted before the final mDirManager->Deref() in this
//     routine.  Failing to do so can cause unwanted recursion
//     and/or attempts to DELETE objects twice.
void ProjectManager::OnCloseWindow(wxCloseEvent & event)
{
   auto &project = mProject;
   auto &projectFileIO = ProjectFileIO::Get( project );
   auto &projectFileManager = ProjectFileManager::Get( project );
   const auto &settings = ProjectSettings::Get( project );
   auto &projectAudioIO = ProjectAudioIO::Get( project );
   auto &tracks = TrackList::Get( project );
   auto &window = ProjectWindow::Get( project );
   auto gAudioIO = AudioIO::Get();

   // We are called for the wxEVT_CLOSE_WINDOW, wxEVT_END_SESSION, and
   // wxEVT_QUERY_END_SESSION, so we have to protect against multiple
   // entries.  This is a hack until the whole application termination
   // process can be reviewed and reworked.  (See bug #964 for ways
   // to exercise the bug that instigated this hack.)
   if (window.IsBeingDeleted())
   {
      event.Skip();
      return;
   }

   if (event.CanVeto() && (::wxIsBusy() || project.mbBusyImporting))
   {
      event.Veto();
      return;
   }

   // Check to see if we were playing or recording
   // audio, and if so, make sure Audio I/O is completely finished.
   // The main point of this is to properly push the state
   // and flush the tracks once we've completely finished
   // recording NEW state.
   // This code is derived from similar code in
   // AudacityProject::~AudacityProject() and TrackPanel::OnTimer().
   if (projectAudioIO.GetAudioIOToken()>0 &&
       gAudioIO->IsStreamActive(projectAudioIO.GetAudioIOToken())) {

      // We were playing or recording audio, but we've stopped the stream.
      wxCommandEvent dummyEvent;
      ControlToolBar::Get( project ).OnStop(dummyEvent);

      window.FixScrollbars();
      projectAudioIO.SetAudioIOToken(0);
      window.RedrawProject();
   }
   else if (gAudioIO->IsMonitoring()) {
      gAudioIO->StopStream();
   }

   // MY: Use routine here so other processes can make same check
   bool bHasTracks = !tracks.empty();

   // We may not bother to prompt the user to save, if the
   // project is now empty.
   if (event.CanVeto() && (settings.EmptyCanBeDirty() || bHasTracks)) {
      if ( UndoManager::Get( project ).UnsavedChanges() ) {
         TitleRestorer Restorer( window, project );// RAII
         /* i18n-hint: The first %s numbers the project, the second %s is the project name.*/
         wxString Title =  wxString::Format(_("%sSave changes to %s?"), Restorer.sProjNumber, Restorer.sProjName);
         wxString Message = _("Save project before closing?");
         if( !bHasTracks )
         {
          Message += _("\nIf saved, the project will have no tracks.\n\nTo save any previously open tracks:\nCancel, Edit > Undo until all tracks\nare open, then File > Save Project.");
         }
         int result = AudacityMessageBox( Message,
                                    Title,
                                   wxYES_NO | wxCANCEL | wxICON_QUESTION,
                                   &window);

         if (result == wxCANCEL || (result == wxYES &&
              !GuardedCall<bool>( [&]{ return projectFileManager.Save(); } )
         )) {
            event.Veto();
            return;
         }
      }
   }

#ifdef __WXMAC__
   // Fix bug apparently introduced into 2.1.2 because of wxWidgets 3:
   // closing a project that was made full-screen (as by clicking the green dot
   // or command+/; not merely "maximized" as by clicking the title bar or
   // Zoom in the Window menu) leaves the screen black.
   // Fix it by un-full-screening.
   // (But is there a different way to do this? What do other applications do?
   //  I don't see full screen windows of Safari shrinking, but I do see
   //  momentary blackness.)
   window.ShowFullScreen(false);
#endif

   ModuleManager::Get().Dispatch(ProjectClosing);

   // Stop the timer since there's no need to update anything anymore
   mTimer.reset();

   // The project is now either saved or the user doesn't want to save it,
   // so there's no need to keep auto save info around anymore
   projectFileIO.DeleteCurrentAutoSaveFile();

   // DMM: Save the size of the last window the user closes
   //
   // LL: Save before doing anything else to the window that might make
   //     its size change.
   SaveWindowSize();

   window.SetIsBeingDeleted();

   // Mac: we never quit as the result of a close.
   // Other systems: we quit only when the close is the result of an external
   // command (on Windows, those are taskbar closes, "X" box, Alt+F4, etc.)
   bool quitOnClose;
#ifdef __WXMAC__
   quitOnClose = false;
#else
   quitOnClose = !projectFileManager.GetMenuClose();
#endif

   // DanH: If we're definitely about to quit, clear the clipboard.
   //       Doing this after Deref'ing the DirManager causes problems.
   if ((AllProjects{}.size() == 1) &&
      (quitOnClose || AllProjects::Closing()))
      Clipboard::Get().Clear();

   // JKC: For Win98 and Linux do not detach the menu bar.
   // We want wxWidgets to clean it up for us.
   // TODO: Is there a Mac issue here??
   // SetMenuBar(NULL);

   projectFileManager.CloseLock();

   // Some of the AdornedRulerPanel functions refer to the TrackPanel, so destroy this
   // before the TrackPanel is destroyed. This change was needed to stop Audacity
   // crashing when running with Jaws on Windows 10 1703.
   AdornedRulerPanel::Destroy( project );

   // Destroy the TrackPanel early so it's not around once we start
   // deleting things like tracks and such out from underneath it.
   // Check validity of mTrackPanel per bug 584 Comment 1.
   // Deeper fix is in the Import code, but this failsafes against crash.
   TrackPanel::Destroy( project );

   // Finalize the tool manager before the children since it needs
   // to save the state of the toolbars.
   ToolManager::Get( project ).Destroy();

   window.DestroyChildren();

   TrackFactory::Destroy( project );

   // Delete all the tracks to free up memory and DirManager references.
   tracks.Clear();

   // This must be done before the following Deref() since it holds
   // references to the DirManager.
   UndoManager::Get( project ).ClearStates();

   // MM: Tell the DirManager it can now DELETE itself
   // if it finds it is no longer needed. If it is still
   // used (f.e. by the clipboard), it will recognize this
   // and will destroy itself later.
   //
   // LL: All objects with references to the DirManager should
   //     have been deleted before this.
   DirManager::Destroy( project );

   // Remove self from the global array, but defer destruction of self
   auto pSelf = AllProjects{}.Remove( project );
   wxASSERT( pSelf );

   if (GetActiveProject() == &project) {
      // Find a NEW active project
      if ( !AllProjects{}.empty() ) {
         SetActiveProject(AllProjects{}.begin()->get());
      }
      else {
         SetActiveProject(NULL);
      }
   }

   // Since we're going to be destroyed, make sure we're not to
   // receive audio notifications anymore.
   // PRL:  Maybe all this is unnecessary now that the listener is managed
   // by a weak pointer.
   if ( gAudioIO->GetListener().get() == &ProjectAudioManager::Get( project ) ) {
      auto active = GetActiveProject();
      gAudioIO->SetListener(
         active
            ? ProjectAudioManager::Get( *active ).shared_from_this()
            : nullptr
      );
   }

   if (AllProjects{}.empty() && !AllProjects::Closing()) {

#if !defined(__WXMAC__)
      if (quitOnClose) {
         // Simulate the application Exit menu item
         wxCommandEvent evt{ wxEVT_MENU, wxID_EXIT };
         wxTheApp->AddPendingEvent( evt );
      }
      else {
         sbWindowRectAlreadySaved = false;
         // For non-Mac, always keep at least one project window open
         (void) New();
      }
#endif
   }

   window.Destroy();

   // Destroys this
   pSelf.reset();
}

// PRL: I preserve this handler function for an event that was never sent, but
// I don't know the intention.

void ProjectManager::OnOpenAudioFile(wxCommandEvent & event)
{
   auto &project = mProject;
   auto &window = GetProjectFrame( project );
   const wxString &cmd = event.GetString();

   if (!cmd.empty())
      ProjectFileManager::Get( mProject ).OpenFile(cmd);

   window.RequestUserAttention();
}

// static method, can be called outside of a project
void ProjectManager::OpenFiles(AudacityProject *proj)
{
   /* i18n-hint: This string is a label in the file type filter in the open
    * and save dialogues, for the option that only shows project files created
    * with Audacity. Do not include pipe symbols or .aup (this extension will
    * now be added automatically for the Save Projects dialogues).*/
   auto selectedFiles =
      ProjectFileManager::ShowOpenDialog(_("Audacity projects"), wxT("*.aup"));
   if (selectedFiles.size() == 0) {
      gPrefs->Write(wxT("/LastOpenType"),wxT(""));
      gPrefs->Flush();
      return;
   }

   //sort selected files by OD status.
   //For the open menu we load OD first so user can edit asap.
   //first sort selectedFiles.
   selectedFiles.Sort(CompareNoCaseFileName);
   ODManager::Pauser pauser;

   auto cleanup = finally( [] {
      gPrefs->Write(wxT("/LastOpenType"),wxT(""));
      gPrefs->Flush();
   } );

   for (size_t ff = 0; ff < selectedFiles.size(); ff++) {
      const wxString &fileName = selectedFiles[ff];

      // Make sure it isn't already open.
      if (ProjectFileManager::IsAlreadyOpen(fileName))
         continue; // Skip ones that are already open.

      FileNames::UpdateDefaultPath(FileNames::Operation::Open, fileName);

      // DMM: If the project is dirty, that means it's been touched at
      // all, and it's not safe to open a NEW project directly in its
      // place.  Only if the project is brand-NEW clean and the user
      // hasn't done any action at all is it safe for Open to take place
      // inside the current project.
      //
      // If you try to Open a NEW project inside the current window when
      // there are no tracks, but there's an Undo history, etc, then
      // bad things can happen, including data files moving to the NEW
      // project directory, etc.
      if ( proj && (
         ProjectHistory::Get( *proj ).GetDirty() ||
         !TrackList::Get( *proj ).empty()
      ) )
         proj = nullptr;

      // This project is clean; it's never been touched.  Therefore
      // all relevant member variables are in their initial state,
      // and it's okay to open a NEW project inside this window.
      proj = OpenProject( proj, fileName );
   }
}

AudacityProject *ProjectManager::OpenProject(
   AudacityProject *pProject, const FilePath &fileNameArg, bool addtohistory)
{
   AudacityProject *pNewProject = nullptr;
   if ( ! pProject )
      pProject = pNewProject = New();
   auto cleanup = finally( [&] {
      if( pNewProject )
         GetProjectFrame( *pNewProject ).Close(true);
   } );
   ProjectFileManager::Get( *pProject ).OpenFile( fileNameArg, addtohistory );
   pNewProject = nullptr;
   auto &projectFileIO = ProjectFileIO::Get( *pProject );
   if( projectFileIO.IsRecovered() ) {
      auto &window = ProjectWindow::Get( *pProject );
      window.Zoom( window.GetZoomOfToFit() );
   }

   return pProject;
}

// This is done to empty out the tracks, but without creating a new project.
void ProjectManager::ResetProjectToEmpty() {
   auto &project = mProject;
   auto &projectFileIO = ProjectFileIO::Get( project );
   auto &projectFileManager = ProjectFileManager::Get( project );
   auto &projectHistory = ProjectHistory::Get( project );
   auto &viewInfo = ViewInfo::Get( project );

   SelectUtilities::DoSelectAll( project );
   TrackUtilities::DoRemoveTracks( project );

   // A new DirManager.
   DirManager::Reset( project );
   TrackFactory::Reset( project );

   projectFileManager.Reset();

   projectHistory.SetDirty( false );
   auto &undoManager = UndoManager::Get( project );
   undoManager.ClearStates();
}

void ProjectManager::RestartTimer()
{
   if (mTimer) {
      // mTimer->Stop(); // not really needed
      mTimer->Start( 3000 ); // Update messages as needed once every 3 s.
   }
}

void ProjectManager::OnTimer(wxTimerEvent& WXUNUSED(event))
{
   auto &project = mProject;
   auto &projectAudioIO = ProjectAudioIO::Get( project );
   auto &window = GetProjectFrame( project );
   auto &dirManager = DirManager::Get( project );
   auto mixerToolBar = &MixerToolBar::Get( project );
   mixerToolBar->UpdateControls();
   
   auto &statusBar = *window.GetStatusBar();

   auto gAudioIO = AudioIO::Get();
   // gAudioIO->GetNumCaptureChannels() should only be positive
   // when we are recording.
   if (projectAudioIO.GetAudioIOToken() > 0 && gAudioIO->GetNumCaptureChannels() > 0) {
      wxLongLong freeSpace = dirManager.GetFreeDiskSpace();
      if (freeSpace >= 0) {
         wxString sMessage;

         int iRecordingMins = GetEstimatedRecordingMinsLeftOnDisk(gAudioIO->GetNumCaptureChannels());
         sMessage.Printf(_("Disk space remaining for recording: %s"), GetHoursMinsString(iRecordingMins));

         // Do not change mLastMainStatusMessage
         statusBar.SetStatusText(sMessage, mainStatusBarField);
      }
   }
   else if(ODManager::IsInstanceCreated())
   {
      //if we have some tasks running, we should say something about it.
      int numTasks = ODManager::Instance()->GetTotalNumTasks();
      if(numTasks)
      {
         wxString msg;
         float ratioComplete= ODManager::Instance()->GetOverallPercentComplete();

         if(ratioComplete>=1.0f)
         {
            //if we are 100 percent complete and there is still a task in the queue, we should wake the ODManager
            //so it can clear it.
            //signal the od task queue loop to wake up so it can remove the tasks from the queue and the queue if it is empty.
            ODManager::Instance()->SignalTaskQueueLoop();


            msg = _("On-demand import and waveform calculation complete.");
            statusBar.SetStatusText(msg, mainStatusBarField);

         }
         else if(numTasks>1)
            msg.Printf(_("Import(s) complete. Running %d on-demand waveform calculations. Overall %2.0f%% complete."),
              numTasks,ratioComplete*100.0);
         else
            msg.Printf(_("Import complete. Running an on-demand waveform calculation. %2.0f%% complete."),
             ratioComplete*100.0);


         statusBar.SetStatusText(msg, mainStatusBarField);
      }
   }

   // As also with the TrackPanel timer:  wxTimer may be unreliable without
   // some restarts
   RestartTimer();
}

void ProjectManager::OnStatusChange( wxCommandEvent & )
{
   auto &project = mProject;
   auto &window = GetProjectFrame( project );
   const auto &msg = project.GetStatus();
   window.GetStatusBar()->SetStatusText(msg, mainStatusBarField);
   
   // When recording, let the NEW status message stay at least as long as
   // the timer interval (if it is not replaced again by this function),
   // before replacing it with the message about remaining disk capacity.
   RestartTimer();
}

wxString ProjectManager::GetHoursMinsString(int iMinutes)
{
   wxString sFormatted;

   if (iMinutes < 1) {
      // Less than a minute...
      sFormatted = _("Less than 1 minute");
      return sFormatted;
   }

   // Calculate
   int iHours = iMinutes / 60;
   int iMins = iMinutes % 60;

   auto sHours =
      wxString::Format( wxPLURAL("%d hour", "%d hours", iHours), iHours );
   auto sMins =
      wxString::Format( wxPLURAL("%d minute", "%d minutes", iMins), iMins );

   /* i18n-hint: A time in hours and minutes. Only translate the "and". */
   sFormatted.Printf( _("%s and %s."), sHours, sMins);
   return sFormatted;
}

// This routine will give an estimate of how many
// minutes of recording time we have available.
// The calculations made are based on the user's current
// preferences.
int ProjectManager::GetEstimatedRecordingMinsLeftOnDisk(long lCaptureChannels) {
   auto &project = mProject;

   // Obtain the current settings
   auto oCaptureFormat = QualityPrefs::SampleFormatChoice();
   if (lCaptureChannels == 0) {
      gPrefs->Read(wxT("/AudioIO/RecordChannels"), &lCaptureChannels, 2L);
   }

   // Find out how much free space we have on disk
   wxLongLong lFreeSpace = DirManager::Get( project ).GetFreeDiskSpace();
   if (lFreeSpace < 0) {
      return 0;
   }

   // Calculate the remaining time
   double dRecTime = 0.0;
   double bytesOnDiskPerSample = SAMPLE_SIZE_DISK(oCaptureFormat);
   dRecTime = lFreeSpace.GetHi() * 4294967296.0 + lFreeSpace.GetLo();
   dRecTime /= bytesOnDiskPerSample;   
   dRecTime /= lCaptureChannels;
   dRecTime /= ProjectSettings::Get( project ).GetRate();

   // Convert to minutes before returning
   int iRecMins = (int)round(dRecTime / 60.0);
   return iRecMins;
}
