#include <string>
#include <iostream>

#include "vsg/all.h"


class ExtendedRasterizationState : public vsg::Inherit<vsg::RasterizationState, ExtendedRasterizationState>
{
public:
    ExtendedRasterizationState() {}
    ExtendedRasterizationState(const ExtendedRasterizationState& rs) : Inherit(rs) {}

    void apply(vsg::Context& context, VkGraphicsPipelineCreateInfo& pipelineInfo) const override
    {
        /**
         * Calling the base class apply() method instantiates the
         * VkPipelineRasterizationStateCreateInfo object and assigns it to a member of
         * VkGraphicsPipelineCreateInfo. The members of this object are all const and
         * so can not be changed after assignment without a const_cast.
         * That is, we couldn't later attach anything to pNext. Therefore, we are
         * forced here to repeat the code that sets the members of this class.
         */
        //RasterizationState::apply(context, pipelineInfo);

        /// This is the same as RasterizationState::apply() up to the final assignment
        /// to pipelineInfo.pRasterizationState
        auto rasteratizationState = context.scratchMemory->allocate<VkPipelineRasterizationStateCreateInfo>();
        rasteratizationState->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        rasteratizationState->pNext = nullptr; /// this will get reassigned below
        rasteratizationState->flags = 0;
        rasteratizationState->depthClampEnable = depthClampEnable;
        rasteratizationState->rasterizerDiscardEnable = rasterizerDiscardEnable;
        rasteratizationState->polygonMode = polygonMode;
        rasteratizationState->cullMode = cullMode;
        rasteratizationState->frontFace = frontFace;
        rasteratizationState->depthBiasEnable = depthBiasEnable;
        rasteratizationState->depthBiasConstantFactor = depthBiasConstantFactor;
        rasteratizationState->depthBiasClamp = depthBiasClamp;
        rasteratizationState->depthBiasSlopeFactor = depthBiasSlopeFactor;
        rasteratizationState->lineWidth = lineWidth;

        /// setup extension feature (stippling) for attachment to pNext below
        auto rastLineStateCreateInfo = context.scratchMemory->allocate<VkPipelineRasterizationLineStateCreateInfoEXT>(1);
        rastLineStateCreateInfo->sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_LINE_STATE_CREATE_INFO_EXT;
        rastLineStateCreateInfo->pNext = nullptr;
        rastLineStateCreateInfo->lineRasterizationMode = VK_LINE_RASTERIZATION_MODE_DEFAULT_EXT;
        rastLineStateCreateInfo->stippledLineEnable = VK_TRUE;
        rastLineStateCreateInfo->lineStippleFactor = 4;
        rastLineStateCreateInfo->lineStipplePattern = 0b1111111100000000;

        rasteratizationState->pNext = rastLineStateCreateInfo; /// must be done before assignment to pRasterizationState

        pipelineInfo.pRasterizationState = rasteratizationState;
    }

protected:
    virtual ~ExtendedRasterizationState() {}
};

std::string VERT{R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable
layout(push_constant) uniform PushConstants { mat4 projection; mat4 modelView; };
layout(location = 0) in vec3 vertex;
out gl_PerVertex { vec4 gl_Position; };
void main() { gl_Position = (projection * modelView) * vec4(vertex, 1.0); }
)"};

std::string FRAG{R"(
#version 450
#extension GL_ARB_separate_shader_objects : enable
layout(location = 0) out vec4 color;
void main() { color = vec4(1, 0, 0, 1); }
)"};

int main(int argc, char** argv)
{
    auto windowTraits = vsg::WindowTraits::create();
    /// set up defaults and read command line arguments to override them
    vsg::CommandLine arguments(&argc, argv);
    windowTraits->debugLayer = arguments.read({"--debug", "-d"});
    windowTraits->apiDumpLayer = arguments.read({"--api", "-a"});
    windowTraits->windowTitle = "vsgextendstate";
    auto window = vsg::Window::create(windowTraits);

    auto physicalDevice = window->getOrCreatePhysicalDevice();
    if (!physicalDevice->supportsDeviceExtension(VK_EXT_LINE_RASTERIZATION_EXTENSION_NAME))
    {
        std::cout << "Line Rasterization Extension not supported.\n";
        return 1;
    }

    auto requestFeatures = windowTraits->deviceFeatures = vsg::DeviceFeatures::create();

    /// enable wideLines feature
    requestFeatures->get().wideLines = VK_TRUE;

    /// enable stippled line extension feature
    auto& lineRasterizationFeatures = requestFeatures->get<
        VkPhysicalDeviceLineRasterizationFeaturesEXT,
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_LINE_RASTERIZATION_FEATURES_EXT>();
    lineRasterizationFeatures.stippledRectangularLines = VK_TRUE;

    auto sceneGraph = vsg::Group::create();

    auto vertexShader = vsg::ShaderStage::create(VK_SHADER_STAGE_VERTEX_BIT, "main", VERT);
    auto fragmentShader = vsg::ShaderStage::create(VK_SHADER_STAGE_FRAGMENT_BIT, "main", FRAG);
    auto shaderSet = vsg::ShaderSet::create(vsg::ShaderStages{vertexShader, fragmentShader});
    shaderSet->addPushConstantRange("pc", "", VK_SHADER_STAGE_VERTEX_BIT, 0, 128);
    shaderSet->addAttributeBinding("vertex", "", 0, VK_FORMAT_R32G32B32_SFLOAT, vsg::vec3Array::create(1));

    auto stateGroup = vsg::StateGroup::create();
    auto gpConf = vsg::GraphicsPipelineConfigurator::create(shaderSet);

    auto vertices = vsg::vec3Array::create({
        {0, 0, 0},
        {1, 0, 0},
        {0, 1, 0},
        {0, 0, 1},
    });
    vsg::DataList vertexArrays;
    gpConf->assignArray(vertexArrays, "vertex", VK_VERTEX_INPUT_RATE_VERTEX, vertices);
    auto vertexDraw = vsg::VertexDraw::create();
    vertexDraw->assignArrays(vertexArrays);
    vertexDraw->vertexCount = vertices->width();
    vertexDraw->instanceCount = 1;
    stateGroup->addChild(vertexDraw);

    struct SetPipelineStates : public vsg::Visitor
    {
        void apply(vsg::Object& object) { object.traverse(*this); }
        void apply(vsg::RasterizationState& rs)
        {
            rs.lineWidth = 10.0f;
            rs.cullMode = VK_CULL_MODE_NONE;
        }
        void apply(vsg::InputAssemblyState& ias)
        {
            ias.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
        }
    } sps;

    /// apply our custom RasterizationState to the GraphicsPipeline
    auto rs = ExtendedRasterizationState::create();
    gpConf->pipelineStates.push_back(rs);

    gpConf->accept(sps);
    gpConf->init();
    gpConf->copyTo(stateGroup);
    sceneGraph->addChild(stateGroup);

    auto lookAt = vsg::LookAt::create(
        vsg::dvec3{3, 2, 2},
        vsg::dvec3{0.2, 0.2, 0.2},
        vsg::dvec3{0, 0, 1}
    );
    auto perspective = vsg::Perspective::create();
    auto viewportState = vsg::ViewportState::create(window->extent2D());
    auto camera = vsg::Camera::create(perspective, lookAt, viewportState);
    auto viewer = vsg::Viewer::create();

    viewer->addWindow(window);
    viewer->addEventHandler(vsg::CloseHandler::create(viewer));
    viewer->addEventHandler(vsg::Trackball::create(camera));

    auto commandGraph = vsg::createCommandGraphForView(window, camera, sceneGraph);
    viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});
    viewer->compile();

    while (viewer->advanceToNextFrame())
    {
        viewer->handleEvents();
        viewer->update();
        viewer->recordAndSubmit();
        viewer->present();
    }
}
