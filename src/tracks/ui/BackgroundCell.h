/**********************************************************************

Audacity: A Digital Audio Editor

BackgroundCell.h

Paul Licameli split from TrackPanel.cpp

**********************************************************************/

#ifndef __AUDACITY_BACKGROUND_CELL__
#define __AUDACITY_BACKGROUND_CELL__

#include "../../ClientData.h"
#include "CommonTrackPanelCell.h"

class AudacityProject;

class BackgroundHandle;
class ZoomHandle;


/// \brief Class representing the background of a Track.  It
/// provides the hit test function that tells us what was hit.
class BackgroundCell final
   : public CommonTrackPanelCell
   , public ClientData::Base
{
public:
   static BackgroundCell &Get( AudacityProject &project );
   static const BackgroundCell &Get( const AudacityProject &project );

   BackgroundCell(AudacityProject *pProject)
      : mpProject(pProject)
   {}

   virtual ~BackgroundCell();

protected:
   std::vector<UIHandlePtr> HitTest
      (const TrackPanelMouseState &state,
       const AudacityProject *) override;

   std::shared_ptr<Track> DoFindTrack() override;

private:
   // TrackPanelDrawable implementation
   void Draw(
      TrackPanelDrawingContext &context,
      const wxRect &rect, unsigned iPass ) override;

   wxRect DrawingArea(
      const wxRect &rect, const wxRect &panelRect, unsigned iPass ) override;
   
   AudacityProject *mpProject;

   std::weak_ptr<BackgroundHandle> mHandle;

public:
   // For want of a better place...
   mutable std::weak_ptr<ZoomHandle> mZoomHandle;
};

#endif
