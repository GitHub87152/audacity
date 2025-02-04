/**********************************************************************
 
 Audacity: A Digital Audio Editor
 
 TrackUtilities.cpp
 
 Paul Licameli split from TrackMenus.cpp
 
 **********************************************************************/

#include "TrackUtilities.h"

#include "ProjectHistory.h"
#include "ProjectSettings.h"
#include "ProjectWindow.h"
#include "Track.h"
#include "TrackPanel.h"

namespace TrackUtilities {

void DoRemoveTracks( AudacityProject &project )
{
   auto &tracks = TrackList::Get( project );
   auto &trackPanel = TrackPanel::Get( project );

   std::vector<Track*> toRemove;
   for (auto track : tracks.Selected())
      toRemove.push_back(track);

   // Capture the track preceding the first removed track
   Track *f{};
   if (!toRemove.empty()) {
      auto found = tracks.Find(toRemove[0]);
      f = *--found;
   }

   for (auto track : toRemove)
      tracks.Remove(track);

   if (!f)
      // try to use the last track
      f = *tracks.Any().rbegin();
   if (f) {
      // Try to use the first track after the removal
      auto found = tracks.FindLeader(f);
      auto t = *++found;
      if (t)
         f = t;
   }

   // If we actually have something left, then make sure it's seen
   if (f)
      f->EnsureVisible();

   ProjectHistory::Get( project )
      .PushState(_("Removed audio track(s)"), _("Remove Track"));

   trackPanel.UpdateViewIfNoTracks();
   trackPanel.Refresh(false);
}

void DoTrackMute(AudacityProject &project, Track *t, bool exclusive)
{
   const auto &settings = ProjectSettings::Get( project );
   auto &tracks = TrackList::Get( project );
   auto &trackPanel = TrackPanel::Get( project );

   // Whatever t is, replace with lead channel
   t = *tracks.FindLeader(t);

   // "exclusive" mute means mute the chosen track and unmute all others.
   if (exclusive) {
      for (auto leader : tracks.Leaders<PlayableTrack>()) {
         const auto group = TrackList::Channels(leader);
         bool chosen = (t == leader);
         for (auto channel : group)
            channel->SetMute( chosen ),
            channel->SetSolo( false );
      }
   }
   else {
      // Normal click toggles this track.
      auto pt = dynamic_cast<PlayableTrack *>( t );
      if (!pt)
         return;

      bool wasMute = pt->GetMute();
      for (auto channel : TrackList::Channels(pt))
         channel->SetMute( !wasMute );

      if (settings.IsSoloSimple() || settings.IsSoloNone())
      {
         // We also set a solo indicator if we have just one track / stereo pair playing.
         // in a group of more than one playable tracks.
         // otherwise clear solo on everything.

         auto range = tracks.Leaders<PlayableTrack>();
         auto nPlayableTracks = range.size();
         auto nPlaying = (range - &PlayableTrack::GetMute).size();

         for (auto track : tracks.Any<PlayableTrack>())
            // will set both of a stereo pair
            track->SetSolo( (nPlaying==1) && (nPlayableTracks > 1 ) && !track->GetMute() );
      }
   }
   ProjectHistory::Get( project ).ModifyState(true);

   trackPanel.UpdateAccessibility();
   trackPanel.Refresh(false);
}

void DoTrackSolo(AudacityProject &project, Track *t, bool exclusive)
{
   const auto &settings = ProjectSettings::Get( project );
   auto &tracks = TrackList::Get( project );
   auto &trackPanel = TrackPanel::Get( project );
   
   // Whatever t is, replace with lead channel
   t = *tracks.FindLeader(t);

   const auto pt = dynamic_cast<PlayableTrack *>( t );
   if (!pt)
      return;
   bool bWasSolo = pt->GetSolo();

   bool bSoloMultiple = !settings.IsSoloSimple() ^ exclusive;

   // Standard and Simple solo have opposite defaults:
   //   Standard - Behaves as individual buttons, shift=radio buttons
   //   Simple   - Behaves as radio buttons, shift=individual
   // In addition, Simple solo will mute/unmute tracks
   // when in standard radio button mode.
   if ( bSoloMultiple )
   {
      for (auto channel : TrackList::Channels(pt))
         channel->SetSolo( !bWasSolo );
   }
   else
   {
      // Normal click solo this track only, mute everything else.
      // OR unmute and unsolo everything.
      for (auto leader : tracks.Leaders<PlayableTrack>()) {
         const auto group = TrackList::Channels(leader);
         bool chosen = (t == leader);
         for (auto channel : group) {
            if (chosen) {
               channel->SetSolo( !bWasSolo );
               if( settings.IsSoloSimple() )
                  channel->SetMute( false );
            }
            else {
               channel->SetSolo( false );
               if( settings.IsSoloSimple() )
                  channel->SetMute( !bWasSolo );
            }
         }
      }
   }
   ProjectHistory::Get( project ).ModifyState(true);

   trackPanel.UpdateAccessibility();
   trackPanel.Refresh(false);
}

void DoRemoveTrack(AudacityProject &project, Track * toRemove)
{
   auto &tracks = TrackList::Get( project );
   auto &trackPanel = TrackPanel::Get( project );
   auto &window = ProjectWindow::Get( project );

   // If it was focused, then NEW focus is the next or, if
   // unavailable, the previous track. (The NEW focus is set
   // after the track has been removed.)
   bool toRemoveWasFocused = trackPanel.GetFocusedTrack() == toRemove;
   Track* newFocus{};
   if (toRemoveWasFocused) {
      auto iterNext = tracks.FindLeader(toRemove), iterPrev = iterNext;
      newFocus = *++iterNext;
      if (!newFocus) {
         newFocus = *--iterPrev;
      }
   }

   wxString name = toRemove->GetName();

   auto channels = TrackList::Channels(toRemove);
   // Be careful to post-increment over positions that get erased!
   auto &iter = channels.first;
   while (iter != channels.end())
      tracks.Remove( * iter++ );

   if (toRemoveWasFocused)
      trackPanel.SetFocusedTrack(newFocus);

   ProjectHistory::Get( project ).PushState(
      wxString::Format(_("Removed track '%s.'"),
      name),
      _("Track Remove"));

   window.HandleResize();
   trackPanel.Refresh(false);
}

void DoMoveTrack
(AudacityProject &project, Track* target, MoveChoice choice)
{
   auto &trackPanel = TrackPanel::Get( project );
   auto &tracks = TrackList::Get( project );

   wxString longDesc, shortDesc;

   switch (choice)
   {
   case OnMoveTopID:
      /* i18n-hint: Past tense of 'to move', as in 'moved audio track up'.*/
      longDesc = _("Moved '%s' to Top");
      shortDesc = _("Move Track to Top");

      // TODO: write TrackList::Rotate to do this in one step and avoid emitting
      // an event for each swap
      while (tracks.CanMoveUp(target))
         tracks.Move(target, true);

      break;
   case OnMoveBottomID:
      /* i18n-hint: Past tense of 'to move', as in 'moved audio track up'.*/
      longDesc = _("Moved '%s' to Bottom");
      shortDesc = _("Move Track to Bottom");

      // TODO: write TrackList::Rotate to do this in one step and avoid emitting
      // an event for each swap
      while (tracks.CanMoveDown(target))
         tracks.Move(target, false);

      break;
   default:
      bool bUp = (OnMoveUpID == choice);

      tracks.Move(target, bUp);
      longDesc =
         /* i18n-hint: Past tense of 'to move', as in 'moved audio track up'.*/
         bUp? _("Moved '%s' Up")
         : _("Moved '%s' Down");
      shortDesc =
         /* i18n-hint: Past tense of 'to move', as in 'moved audio track up'.*/
         bUp? _("Move Track Up")
         : _("Move Track Down");

   }

   longDesc = longDesc.Format(target->GetName());

   ProjectHistory::Get( project ).PushState(longDesc, shortDesc);
   trackPanel.Refresh(false);
}

}
