#pragma once
#include <hyprland/src/render/pass/PassElement.hpp>

class COverview;

class COverviewPassElement : public IPassElement {
  public:
    COverviewPassElement()          = default;
    virtual ~COverviewPassElement() = default;

    virtual std::vector<UP<IPassElement>> draw(Render::CRenderContext& ctx) override;
    virtual bool                          needsLiveBlur(Render::CRenderContext& ctx) override;
    virtual bool                          needsPrecomputeBlur(Render::CRenderContext& ctx) override;
    virtual std::optional<CBox>           boundingBox(Render::CRenderContext& ctx) override;
    virtual CRegion                       opaqueRegion(Render::CRenderContext& ctx) override;
    virtual ePassElementType              type() override {
        return EK_CUSTOM;
    }

    virtual const char* passName() override {
        return "COverviewPassElement";
    }
};
