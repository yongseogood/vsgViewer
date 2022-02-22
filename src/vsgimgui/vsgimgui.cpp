#include <vsgImGui/RenderImGui.h>
#include <vsgImGui/SendEventsToImGui.h>
#include <vsgImGui/imgui.h>

#include <vsg/all.h>

#ifdef vsgXchange_FOUND
#    include <vsgXchange/all.h>
#endif

struct Params : public vsg::Inherit<vsg::Object, Params>
{
    bool showGui = true; // you can toggle this with your own EventHandler and key
    bool showDemoWindow = false;
    bool showSecondWindow = false;
    float clearColor[3]{0.2f, 0.2f, 0.4f}; // Unfortunately, this doesn't change dynamically in vsg
    uint32_t counter = 0;
    float dist = 0.f;
};

class MyGuiComponent
{
public:
    MyGuiComponent(vsg::ref_ptr<Params> params) :
        _params(params)
    {
    }

    // Example here taken from the Dear imgui comments (mostly)
    bool operator()()
    {
        bool visibleComponents = false;

        // 2. Show a simple window that we create ourselves. We use a Begin/End pair to created a named window.
        if (_params->showGui)
        {
            ImGui::Begin("Hello, world!"); // Create a window called "Hello, world!" and append into it.

            ImGui::Text("Some useful message here.");                 // Display some text (you can use a format strings too)
            ImGui::Checkbox("Demo Window", &_params->showDemoWindow); // Edit bools storing our window open/close state
            ImGui::Checkbox("Another Window", &_params->showSecondWindow);
            ImGui::SliderFloat("float", &_params->dist, 0.0f, 1.0f);        // Edit 1 float using a slider from 0.0f to 1.0f
            ImGui::ColorEdit3("clear color", (float*)&_params->clearColor); // Edit 3 floats representing a color

            if (ImGui::Button("Button")) // Buttons return true when clicked (most widgets return true when edited/activated)
                _params->counter++;

            ImGui::SameLine();
            ImGui::Text("counter = %d", _params->counter);

            ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
            ImGui::End();

            visibleComponents = true;
        }

        // 3. Show another simple window.
        if (_params->showSecondWindow)
        {
            ImGui::Begin("Another Window", &_params->showSecondWindow); // Pass a pointer to our bool variable (the window will have a closing button that will clear the bool when clicked)
            ImGui::Text("Hello from another window!");
            if (ImGui::Button("Close Me"))
                _params->showSecondWindow = false;
            ImGui::End();

            visibleComponents = true;
        }

        if (_params->showDemoWindow)
        {
            ImGui::ShowDemoWindow(&_params->showDemoWindow);

            visibleComponents = true;
        }

        return visibleComponents;
    }

private:
    vsg::ref_ptr<Params> _params;
};

int main(int argc, char** argv)
{
    // set up defaults and read command line arguments to override them
    auto options = vsg::Options::create();
#ifdef vsgXchange_all
    // add vsgXchange's support for reading and writing 3rd party file formats
    options->add(vsgXchange::all::create());
#endif

    auto windowTraits = vsg::WindowTraits::create();
    windowTraits->windowTitle = "vsgimgui";

    // set up defaults and read command line arguments to override them
    vsg::CommandLine arguments(&argc, argv);
    arguments.read(options);

    auto event_read_filename = arguments.value(std::string(""), "-i");
    auto event_output_filename = arguments.value(std::string(""), "-o");

    windowTraits->debugLayer = arguments.read({"--debug", "-d"});
    windowTraits->apiDumpLayer = arguments.read({"--api", "-a"});
    arguments.read("--screen", windowTraits->screenNum);
    arguments.read("--display", windowTraits->display);
    auto numFrames = arguments.value(-1, "-f");

    if (arguments.errors()) return arguments.writeErrorMessages(std::cerr);

    try
    {
        auto vsg_scene = vsg::Group::create();

        if (argc > 1)
        {
            vsg::Path filename = arguments[1];
            if (auto node = vsg::read_cast<vsg::Node>(filename, options); node)
            {
                vsg_scene->addChild(node);
            }
        }

        // create the viewer and assign window(s) to it
        auto viewer = vsg::Viewer::create();

        vsg::ref_ptr<vsg::Window> window(vsg::Window::create(windowTraits));
        if (!window)
        {
            std::cout << "Could not create windows." << std::endl;
            return 1;
        }

        viewer->addWindow(window);

        // compute the bounds of the scene graph to help position camera
        vsg::ComputeBounds computeBounds;
        vsg_scene->accept(computeBounds);
        vsg::dvec3 centre = (computeBounds.bounds.min + computeBounds.bounds.max) * 0.5;
        double radius = vsg::length(computeBounds.bounds.max - computeBounds.bounds.min) * 0.6;

        // These are set statically because the geometry in the class is expanded in the shader
        double nearFarRatio = 0.01;

        // set up the camera
        auto lookAt = vsg::LookAt::create(centre + vsg::dvec3(0.0, -radius * 3.5, 0.0), centre, vsg::dvec3(0.0, 0.0, 1.0));

        vsg::ref_ptr<vsg::ProjectionMatrix> perspective;
        if (vsg::ref_ptr<vsg::EllipsoidModel> ellipsoidModel(vsg_scene->getObject<vsg::EllipsoidModel>("EllipsoidModel")); ellipsoidModel)
        {
            perspective = vsg::EllipsoidPerspective::create(lookAt, ellipsoidModel, 30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio, 0.0);
        }
        else
        {
            perspective = vsg::Perspective::create(30.0, static_cast<double>(window->extent2D().width) / static_cast<double>(window->extent2D().height), nearFarRatio * radius, radius * 400.5);
        }

        auto camera = vsg::Camera::create(perspective, lookAt, vsg::ViewportState::create(window->extent2D()));

        // The commandGraph will contain a 2 stage renderGraph 1) 3D scene 2) ImGui (by default also includes clear depth buffers)
        auto commandGraph = vsg::CommandGraph::create(window);
        auto renderGraph = vsg::RenderGraph::create(window);
        commandGraph->addChild(renderGraph);

        // create the normal 3D view of the scene
        renderGraph->addChild(vsg::View::create(camera, vsg_scene));

        // Create the ImGui node and add it to the renderGraph
        auto params = Params::create();
        renderGraph->addChild(vsgImGui::RenderImGui::create(window, MyGuiComponent(params)));

        // Add the ImGui event handler first to handle events early
        viewer->addEventHandler(vsgImGui::SendEventsToImGui::create());

        // add close handler to respond the close window button and pressing escape
        viewer->addEventHandler(vsg::CloseHandler::create(viewer));

        viewer->addEventHandler(vsg::Trackball::create(camera));

        viewer->assignRecordAndSubmitTaskAndPresentation({commandGraph});

        viewer->compile();

        vsg::ref_ptr<vsg::RecordEvents> recordEvents;
        if (!event_output_filename.empty())
        {
            recordEvents = vsg::RecordEvents::create();
            viewer->addEventHandler(recordEvents);
        }

        vsg::ref_ptr<vsg::PlayEvents> playEvents;
        if (!event_read_filename.empty())
        {
            auto read_events = vsg::read(event_read_filename);
            if (read_events)
            {
                playEvents = vsg::PlayEvents::create(read_events, viewer->start_point().time_since_epoch());
            }
        }

        // rendering main loop
        while (viewer->advanceToNextFrame() && (numFrames < 0 || (numFrames--) > 0))
        {
            if (playEvents)
            {
                playEvents->dispatchFrameEvents(viewer->getEvents());
            }

            viewer->handleEvents();

            viewer->update();

            viewer->recordAndSubmit();

            viewer->present();
        }

        if (recordEvents && !event_output_filename.empty())
        {
            // shift the time of recorded events to relative to 0, so we can later add in any new viewer->start_point() during playback.
            vsg::ShiftEventTime shiftTime(-viewer->start_point().time_since_epoch());
            recordEvents->events->accept(shiftTime);

            vsg::write(recordEvents->events, event_output_filename);
        }
    }
    catch (const vsg::Exception& ve)
    {
        std::cerr << "[Exception] - " << ve.message << std::endl;
    }

    return 0;
}
