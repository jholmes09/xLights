/***************************************************************
 * This source files comes from the xLights project
 * https://www.xlights.org
 * https://github.com/xLightsSequencer/xLights
 * See the github commit history for a record of contributing
 * developers.
 * Copyright claimed based on commit dates recorded in Github
 * License: https://github.com/xLightsSequencer/xLights/blob/master/License.txt
 **************************************************************/

#include <algorithm>

#include <wx/propgrid/propgrid.h>
#include <wx/propgrid/advprops.h>

#include "DmxFlamePropertyAdapter.h"
#include "shared/utils/wxUtilities.h"
#include "models/DMX/DmxFlame.h"
#include "models/OutputModelManager.h"

namespace {
    const char* const kFixtureIds[] = { FlameFixtures::GFlameId, FlameFixtures::ShowvenId, FlameFixtures::CustomId };
    const wxChar* const kFixtureLabels[] = { wxT("Galaxis G-Flame"), wxT("Showven Circle Flamer"), wxT("Custom (JSON file)"), nullptr };

    int FixtureIndex(const std::string& id) {
        for (int i = 0; i < 3; ++i) {
            if (id == kFixtureIds[i]) {
                return i;
            }
        }
        return 1;
    }
}

DmxFlamePropertyAdapter::DmxFlamePropertyAdapter(Model& model) :
    DmxPropertyAdapter(model), _flame(static_cast<DmxFlame&>(model)) {}

void DmxFlamePropertyAdapter::AddTypeProperties(wxPropertyGridInterface* grid, OutputManager* outputManager) {
    const auto& fx = _flame.GetFixture();

    grid->Append(new wxEnumProperty("Fixture", "FlameFixture", kFixtureLabels, nullptr, FixtureIndex(_flame.GetFixtureId())));

    if (_flame.GetFixtureId() == FlameFixtures::CustomId) {
        auto p = grid->Append(new wxFileProperty("Definition File", "FlameCustomFile", _flame.GetCustomFile()));
        p->SetAttribute(wxPG_FILE_WILDCARD, "Flame fixture (*.json)|*.json|All files (*.*)|*.*");
        if (!_flame.GetCustomError().empty()) {
            auto e = grid->Append(new wxStringProperty("Load Error", "FlameCustomError", _flame.GetCustomError()));
            e->Enable(false);
        }
    }

    std::string chans;
    for (size_t i = 0; i < fx.channels.size(); ++i) {
        if (i) {
            chans += ", ";
        }
        chans += std::to_string(i + 1) + " " + fx.channels[i];
    }
    if (_flame.GetFixtureId() == FlameFixtures::ShowvenId) {
        chans += "  (CH6 pump arm is not driven)";
    } else if (_flame.GetFixtureId() == FlameFixtures::GFlameId) {
        chans += "  (safety channel is not driven)";
    }
    auto c = grid->Append(new wxStringProperty("Channels", "FlameChannels", chans));
    c->Enable(false);

    if (fx.HasAngle() && fx.PositionCount() > 1) {
        auto p = grid->Append(new wxUIntProperty("Leftmost Position", "FlameMinPos", _flame.GetMinPosition()));
        p->SetAttribute("Min", 1);
        p->SetAttribute("Max", fx.PositionCount());
        p->SetEditor("SpinCtrl");
        p->SetHelpString("Matches the unit's own Angle Limit setting. Positions outside the limits can't be picked in the Flame effect.");
        p = grid->Append(new wxUIntProperty("Rightmost Position", "FlameMaxPos", _flame.GetMaxPosition()));
        p->SetAttribute("Min", 1);
        p->SetAttribute("Max", fx.PositionCount());
        p->SetEditor("SpinCtrl");
        auto b = grid->Append(new wxBoolProperty("Flip Left/Right", "FlameFlip", _flame.GetFlip()));
        b->SetAttribute("UseCheckbox", true);
        b->SetHelpString("Turn on if the unit faces away from the preview, so position 1 draws on the right.");
    }

    auto p = grid->Append(new wxUIntProperty("Flame Size %", "FlameSize", _flame.GetFlameSize()));
    p->SetAttribute("Min", 10);
    p->SetAttribute("Max", 400);
    p->SetEditor("SpinCtrl");
}

int DmxFlamePropertyAdapter::OnPropertyGridChange(wxPropertyGridInterface* grid, wxPropertyGridEvent& event) {
    const wxString name = event.GetPropertyName();
    if (name == "FlameFixture") {
        int idx = std::clamp((int)event.GetPropertyValue().GetLong(), 0, 2);
        _flame.SetFixtureId(kFixtureIds[idx]);
        _flame.UpdateChannelCount(_flame.GetFixture().ChannelCount(), true);
        _flame.IncrementChangeCount();
        _flame.AddASAPWork(OutputModelManager::WORK_RELOAD_MODEL_CHANGE | OutputModelManager::WORK_RELOAD_PROPERTYGRID, "DmxFlamePropertyAdapter::Fixture");
        return 0;
    }
    if (name == "FlameCustomFile") {
        _flame.SetCustomFile(ToStdString(event.GetValue().GetString()));
        _flame.UpdateChannelCount(_flame.GetFixture().ChannelCount(), true);
        _flame.IncrementChangeCount();
        _flame.AddASAPWork(OutputModelManager::WORK_RELOAD_MODEL_CHANGE | OutputModelManager::WORK_RELOAD_PROPERTYGRID, "DmxFlamePropertyAdapter::CustomFile");
        return 0;
    }
    if (name == "FlameMinPos" || name == "FlameMaxPos") {
        int v = (int)event.GetPropertyValue().GetLong();
        if (name == "FlameMinPos") {
            _flame.SetMinPosition(v);
        } else {
            _flame.SetMaxPosition(v);
        }
        _flame.IncrementChangeCount();
        _flame.AddASAPWork(OutputModelManager::WORK_RELOAD_MODEL_CHANGE, "DmxFlamePropertyAdapter::AngleLimit");
        return 0;
    }
    if (name == "FlameFlip") {
        _flame.SetFlip(event.GetValue().GetBool());
        _flame.IncrementChangeCount();
        _flame.AddASAPWork(OutputModelManager::WORK_REDRAW_LAYOUTPREVIEW | OutputModelManager::WORK_RGBEFFECTS_CHANGE, "DmxFlamePropertyAdapter::Flip");
        return 0;
    }
    if (name == "FlameSize") {
        _flame.SetFlameSize((int)event.GetPropertyValue().GetLong());
        _flame.IncrementChangeCount();
        _flame.AddASAPWork(OutputModelManager::WORK_REDRAW_LAYOUTPREVIEW | OutputModelManager::WORK_RGBEFFECTS_CHANGE, "DmxFlamePropertyAdapter::Size");
        return 0;
    }
    return DmxPropertyAdapter::OnPropertyGridChange(grid, event);
}
