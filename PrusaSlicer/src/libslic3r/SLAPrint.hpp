#ifndef slic3r_SLAPrint_hpp_
#define slic3r_SLAPrint_hpp_

#include <cstdint>
#include <mutex>
#include <set>

#include "PrintBase.hpp"
#include "SLA/SupportTree.hpp"
#include "Point.hpp"
#include "Format/SLAArchiveWriter.hpp"
#include "GCode/ThumbnailData.hpp"
#include "libslic3r/CSGMesh/CSGMesh.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/OpenVDBUtils.hpp"

#include <boost/functional/hash.hpp>

namespace Slic3r {

enum SLAPrintStep : unsigned int {
    slapsMergeSlicesAndEval,
    slapsRasterize,
	slapsCount
};

enum SLAPrintObjectStep : unsigned int {
    slaposAssembly,
    slaposHollowing,
    slaposDrillHoles,
	slaposObjectSlice,
	slaposSupportPoints,
	slaposSupportTree,
	slaposPad,
    slaposSliceSupports,
	slaposCount
};

class SLAPrint;
class GLCanvas;

using _SLAPrintObjectBase =
    PrintObjectBaseWithState<SLAPrint, SLAPrintObjectStep, slaposCount>;

// Layers according to quantized height levels. This will be consumed by
// the printer (rasterizer) in the SLAPrint class.
// using coord_t = int64_t;

enum SliceOrigin { soSupport, soModel };

} // namespace Slic3r

namespace Slic3r {

// Each sla object step can hold a collection of csg operations on the
// sla model to be sliced. Currently, Assembly step adds negative and positive
// volumes, hollowing adds the negative interior, drilling adds the hole cylinders.
// They need to be processed in this specific order. If CSGPartForStep instances
// are put into a multiset container the key being the sla step,
// iterating over the container will maintain the correct order of csg operations.
struct CSGPartForStep : public csg::CSGPart
{
    SLAPrintObjectStep key;
    mutable MeshBoolean::cgal::CGALMeshPtr cgalcache;

    CSGPartForStep(SLAPrintObjectStep k, CSGPart &&p = {})
        : key{k}, CSGPart{std::move(p)}
    {}

    CSGPartForStep &operator=(CSGPart &&part)
    {
        this->its_ptr = std::move(part.its_ptr);
        this->operation = part.operation;

        return *this;
    }

    bool operator<(const CSGPartForStep &other) const { return key < other.key; }
};

namespace csg {

MeshBoolean::cgal::CGALMeshPtr get_cgalmesh(const CSGPartForStep &part);

} // namespace csg

class SLAPrintObject : public _SLAPrintObjectBase
{
private: // Prevents erroneous use by other classes.
    using Inherited = _SLAPrintObjectBase;
    using CSGContainer = std::multiset<CSGPartForStep>;

public:

    // I refuse to grantee copying (Tamas)
    SLAPrintObject(const SLAPrintObject&) = delete;
    SLAPrintObject& operator=(const SLAPrintObject&) = delete;

    const SLAPrintObjectConfig& config() const { return m_config; }
    const Transform3d&          trafo()  const { return m_trafo; }
    bool                        is_left_handed() const { return m_left_handed; }

    struct Instance {
        Instance(ObjectID inst_id, const Point &shft, float rot) : instance_id(inst_id), shift(shft), rotation(rot) {}
        bool operator==(const Instance &rhs) const { return this->instance_id == rhs.instance_id && this->shift == rhs.shift && this->rotation == rhs.rotation; }
        // ID of the corresponding ModelInstance.
        ObjectID instance_id;
        // Slic3r::Point objects in scaled G-code coordinates
        Point 	shift;
        // Rotation along the Z axis, in radians.
        float 	rotation;
    };
    const std::vector<Instance>& instances() const { return m_instances; }

    // Get a support mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposSupportTree) is true.
    const TriangleMesh&     support_mesh() const;
    // Get a pad mesh centered around origin in XY, and with zero rotation around Z applied.
    // Support mesh is only valid if this->is_step_done(slaposPad) is true.
    const TriangleMesh&     pad_mesh() const;

    // Get the mesh that is going to be printed with all the modifications
    // like hollowing and drilled holes.
    const std::shared_ptr<const indexed_triangle_set>& get_mesh_to_print() const;

    std::vector<csg::CSGPart> get_parts_to_slice() const;

    std::vector<csg::CSGPart> get_parts_to_slice(SLAPrintObjectStep step) const;

    sla::SupportPoints      transformed_support_points() const;
    sla::DrainHoles         transformed_drainhole_points() const;

    // Get the needed Z elevation for the model geometry if supports should be
    // displayed. This Z offset should also be applied to the support
    // geometries. Note that this is not the same as the value stored in config
    // as the pad height also needs to be considered.
    double get_elevation() const;

    // This method returns the needed elevation according to the processing
    // status. If the supports are not ready, it is zero, if they are and the
    // pad is not, then without the pad, otherwise the full value is returned.
    double get_current_elevation() const;

    // This method returns the support points of this SLAPrintObject.
    const std::vector<sla::SupportPoint>& get_support_points() const;

    // The public Slice record structure. It corresponds to one printable layer.
    class SliceRecord {
    public:
        // this will be the max limit of size_t
        static const size_t NONE = size_t(-1);

        static const SliceRecord EMPTY;

    private:
        coord_t   m_print_z = 0;      // Top of the layer
        float     m_slice_z = 0.f;    // Exact level of the slice
        float     m_height  = 0.f;     // Height of the sliced layer

        size_t m_model_slices_idx = NONE;
        size_t m_support_slices_idx = NONE;
        const SLAPrintObject *m_po = nullptr;

    public:

        SliceRecord(coord_t key, float slicez, float height):
            m_print_z(key), m_slice_z(slicez), m_height(height) {}

        // The key will be the integer height level of the top of the layer.
        coord_t print_level() const { return m_print_z; }

        // Returns the exact floating point Z coordinate of the slice
        float slice_level() const { return m_slice_z; }

        // Returns the current layer height
        float layer_height() const { return m_height; }

        bool is_valid() const { return m_po && ! std::isnan(m_slice_z); }

        const SLAPrintObject* print_obj() const { return m_po; }

        // Methods for setting the indices into the slice vectors.
        void set_model_slice_idx(const SLAPrintObject &po, size_t id) {
            m_po = &po; m_model_slices_idx = id;
        }

        void set_support_slice_idx(const SLAPrintObject& po, size_t id) {
            m_po = &po; m_support_slices_idx = id;
        }

        const ExPolygons& get_slice(SliceOrigin o) const;
        size_t            get_slice_idx(SliceOrigin o) const
        {
            return o == soModel ? m_model_slices_idx : m_support_slices_idx;
        }
    };

private:
    template<class T> inline static T level(const SliceRecord &sr)
    {
        static_assert(std::is_arithmetic<T>::value, "Arithmetic only!");
        return std::is_integral<T>::value ? T(sr.print_level())
                                          : T(sr.slice_level());
    }

    template<class T> inline static SliceRecord create_slice_record(T val)
    {
        static_assert(std::is_arithmetic<T>::value, "Arithmetic only!");
        return std::is_integral<T>::value
                   ? SliceRecord{coord_t(val), 0.f, 0.f}
                   : SliceRecord{0, float(val), 0.f};
    }

    // This is a template method for searching the slice index either by
    // an integer key: print_level or a floating point key: slice_level.
    // The eps parameter gives the max deviation in + or - direction.
    //
    // This method can be used in const or non-const contexts as well.
    template<class Container, class T>
    static auto closest_slice_record(
            Container& cont,
            T lvl,
            T eps = std::numeric_limits<T>::max()) -> decltype (cont.begin())
    {
        if(cont.empty()) return cont.end();
        if(cont.size() == 1 && std::abs(level<T>(cont.front()) - lvl) > eps)
            return cont.end();

        SliceRecord query = create_slice_record(lvl);

        auto it = std::lower_bound(cont.begin(), cont.end(), query,
                                   [](const SliceRecord& r1,
                                      const SliceRecord& r2)
        {
            return level<T>(r1) < level<T>(r2);
        });
        
        if(it == cont.end()) return it;

        T diff = std::abs(level<T>(*it) - lvl);

        if(it != cont.begin()) {
            auto it_prev = std::prev(it);
            T diff_prev = std::abs(level<T>(*it_prev) - lvl);
            if(diff_prev < diff) { diff = diff_prev; it = it_prev; }
        }

        if(diff > eps) it = cont.end();

        return it;
    }

    const std::vector<ExPolygons>& get_model_slices() const { return m_model_slices; }
    const std::vector<ExPolygons>& get_support_slices() const;

public:

    // /////////////////////////////////////////////////////////////////////////
    //
    // These methods should be callable on the client side (e.g. UI thread)
    // when the appropriate steps slaposObjectSlice and slaposSliceSupports
    // are ready. All the print objects are processed before slapsRasterize so
    // it is safe to call them during and/or after slapsRasterize.
    //
    // /////////////////////////////////////////////////////////////////////////

    // Retrieve the slice index.
    const std::vector<SliceRecord>& get_slice_index() const {
        return m_slice_index;
    }

    // Search slice index for the closest slice to given print_level.
    // max_epsilon gives the allowable deviation of the returned slice record's
    // level.
    const SliceRecord& closest_slice_to_print_level(
            coord_t print_level,
            coord_t max_epsilon = std::numeric_limits<coord_t>::max()) const
    {
        auto it = closest_slice_record(m_slice_index, print_level, max_epsilon);
        return it == m_slice_index.end() ? SliceRecord::EMPTY : *it;
    }

    // Search slice index for the closest slice to given slice_level.
    // max_epsilon gives the allowable deviation of the returned slice record's
    // level. Use SliceRecord::is_valid() to check the result.
    const SliceRecord& closest_slice_to_slice_level(
            float slice_level,
            float max_epsilon = std::numeric_limits<float>::max()) const
    {
        auto it = closest_slice_record(m_slice_index, slice_level, max_epsilon);
        return it == m_slice_index.end() ? SliceRecord::EMPTY : *it;
    }

protected:
    // to be called from SLAPrint only.
    friend class SLAPrint;
    friend class PrintBaseWithState<SLAPrintStep, slapsCount>;

	SLAPrintObject(SLAPrint* print, ModelObject* model_object);
    ~SLAPrintObject();

    void                    config_apply(const ConfigBase &other, bool ignore_nonexistent = false) { m_config.apply(other, ignore_nonexistent); }
    void                    config_apply_only(const ConfigBase &other, const t_config_option_keys &keys, bool ignore_nonexistent = false)
        { m_config.apply_only(other, keys, ignore_nonexistent); }

    void                    set_trafo(const Transform3d& trafo, bool left_handed) {
        m_trafo = trafo;
        m_left_handed = left_handed;
    }

    template<class InstVec> inline void set_instances(InstVec&& instances) { m_instances = std::forward<InstVec>(instances); }

    // Invalidates the step, and its depending steps in SLAPrintObject and SLAPrint.
    bool                    invalidate_step(SLAPrintObjectStep step);
    bool                    invalidate_all_steps();
    // Invalidate steps based on a set of parameters changed.
    bool                    invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys);

private:
    // Object specific configuration, pulled from the configuration layer.
    SLAPrintObjectConfig                    m_config;

    // Translation in Z + Rotation by Y and Z + Scaling / Mirroring.
    Transform3d                             m_trafo = Transform3d::Identity();
    // m_trafo is left handed -> 3x3 affine transformation has negative determinant.
    bool                                    m_left_handed = false;

    std::vector<Instance> 					m_instances;

    // Individual 2d slice polygons from lower z to higher z levels
    std::vector<ExPolygons>                 m_model_slices;

    // Exact (float) height levels mapped to the slices. Each record contains
    // the index to the model and the support slice vectors.
    std::vector<SliceRecord>                m_slice_index;

    std::vector<float>                      m_model_height_levels;

    struct SupportData
    {
        sla::SupportableMesh    input; // the input
        std::vector<ExPolygons> support_slices;   // sliced supports
        TriangleMesh tree_mesh, pad_mesh, full_mesh; // cached artifacts
        
        inline SupportData(const TriangleMesh &t)
            : input{t.its, {}, {}}
        {}

        inline SupportData(const indexed_triangle_set &t)
            : input{t, {}, {}}
        {}
        
        void create_support_tree(const sla::JobController &ctl)
        {
            tree_mesh = TriangleMesh{sla::create_support_tree(input, ctl)};
        }

        void create_pad(const sla::JobController &ctl)
        {
            pad_mesh = TriangleMesh{sla::create_pad(input, tree_mesh.its, ctl)};
        }
    };

    std::unique_ptr<SupportData>  m_supportdata;

    // Holds CSG operations for the printed object, prioritized by print steps.
    CSGContainer                  m_mesh_to_slice;

    auto mesh_to_slice(SLAPrintObjectStep s) const
    {
        auto r = m_mesh_to_slice.equal_range(s);

        return Range{r.first, r.second};
    }

    auto mesh_to_slice() const { return range(m_mesh_to_slice); }

    // Holds the preview of the object to be printed (as it will look like with
    // all its holes and cavities, negatives and positive volumes unified.
    // Essentially this should be a m_mesh_to_slice after the CSG operations
    // or an approximation of that.
    std::array<std::shared_ptr<const indexed_triangle_set>, SLAPrintObjectStep::slaposCount + 1> m_preview_meshes;

    class HollowingData
    {
    public:

        sla::InteriorPtr interior;
    };
    
    std::unique_ptr<HollowingData> m_hollowing_data;
};

using PrintObjects = std::vector<SLAPrintObject*>;

using SliceRecord  = SLAPrintObject::SliceRecord;

class TriangleMesh;

struct SLAPrintStatistics
{
    SLAPrintStatistics() { clear(); }
    double                          estimated_print_time;
    double                          objects_used_material;
    double                          support_used_material;
    size_t                          slow_layers_count;
    size_t                          fast_layers_count;
    double                          total_cost;
    double                          total_weight;
    std::vector<double>             layers_times;

    // Config with the filled in print statistics.
    DynamicConfig           config() const;
    // Config with the statistics keys populated with placeholder strings.
    static DynamicConfig    placeholders();
    // Replace the print statistics placeholders in the path.
    std::string             finalize_output_path(const std::string &path_in) const;

    void clear() {
        estimated_print_time = 0.;
        objects_used_material = 0.;
        support_used_material = 0.;
        slow_layers_count = 0;
        fast_layers_count = 0;
        total_cost = 0.;
        total_weight = 0.;
        layers_times.clear();
    }
};

/**
 * @brief This class is the high level FSM for the SLA printing process.
 *
 * It should support the background processing framework and contain the
 * metadata for the support geometries and their slicing. It should also
 * dispatch the SLA printing configuration values to the appropriate calculation
 * steps.
 */
class SLAPrint : public PrintBaseWithState<SLAPrintStep, slapsCount>
{
private: // Prevents erroneous use by other classes.
    typedef PrintBaseWithState<SLAPrintStep, slapsCount> Inherited;
    
    class Steps; // See SLAPrintSteps.cpp
    
public:

    SLAPrint() = default;

    virtual ~SLAPrint() override { this->clear(); }

    PrinterTechnology	technology() const noexcept override { return ptSLA; }

    void                clear() override;
    bool                empty() const override { return m_objects.empty(); }
    // List of existing PrintObject IDs, to remove notifications for non-existent IDs.
    std::vector<ObjectID> print_object_ids() const override;
    ApplyStatus         apply(const Model &model, DynamicPrintConfig config) override;
    void                set_task(const TaskParams &params) override { PrintBaseWithState<SLAPrintStep, slapsCount>::set_task_impl(params, m_objects); }
    void                process() override;
    void                finalize() override { PrintBaseWithState<SLAPrintStep, slapsCount>::finalize_impl(m_objects); }
    void                cleanup() override {}
    // Returns true if an object step is done on all objects and there's at least one object.
    bool                is_step_done(SLAPrintObjectStep step) const;
    // Returns true if the last step was finished with success.
    bool                finished() const override { return this->is_step_done(slaposSliceSupports) && this->Inherited::is_step_done(slapsRasterize); }

    const PrintObjects& objects() const { return m_objects; }
    // PrintObject by its ObjectID, to be used to uniquely bind slicing warnings to their source PrintObjects
    // in the notification center.
    const SLAPrintObject* get_print_object_by_model_object_id(ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [object_id](const SLAPrintObject* obj) { return obj->model_object()->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }
    const SLAPrintObject* get_object(ObjectID object_id) const {
        auto it = std::find_if(m_objects.begin(), m_objects.end(),
            [object_id](const SLAPrintObject *obj) { return obj->id() == object_id; });
        return (it == m_objects.end()) ? nullptr : *it;
    }

    const SLAPrintConfig&       print_config() const { return m_print_config; }
    const SLAPrinterConfig&     printer_config() const { return m_printer_config; }
    const SLAMaterialConfig&    material_config() const { return m_material_config; }
    const SLAPrintObjectConfig& default_object_config() const { return m_default_object_config; }

    // Extracted value from the configuration objects
    Vec3d                       relative_correction() const;

    // Return sla tansformation for a given model_object
    Transform3d sla_trafo(const ModelObject &model_object) const;

	std::string                 output_filename(const std::string &filename_base = std::string()) const override;

    const SLAPrintStatistics&   print_statistics() const { return m_print_statistics; }

    std::string validate(std::vector<std::string>* warnings = nullptr) const override;

    // An aggregation of SliceRecord-s from all the print objects for each
    // occupied layer. Slice record levels dont have to match exactly.
    // They are unified if the level difference is within +/- SCALED_EPSILON
    class PrintLayer {
        coord_t m_level;

        // The collection of slice records for the current level.
        std::vector<std::reference_wrapper<const SliceRecord>> m_slices;

        ExPolygons m_transformed_slices;

        template<class Container> void transformed_slices(Container&& c)
        {
            m_transformed_slices = std::forward<Container>(c);
        }
        
        friend class SLAPrint::Steps;

    public:
        
        explicit PrintLayer(coord_t lvl) : m_level(lvl) {}

        // for being sorted in their container (see m_printer_input)
        bool operator<(const PrintLayer& other) const {
            return m_level < other.m_level;
        }

        void add(const SliceRecord& sr) { m_slices.emplace_back(sr); }

        coord_t level() const { return m_level; }

        auto slices() const -> const decltype (m_slices)& { return m_slices; }

        const ExPolygons & transformed_slices() const {
            return m_transformed_slices;
        }
    };

    // The aggregated and leveled print records from various objects.
    // TODO: use this structure for the preview in the future.
    const std::vector<PrintLayer>& print_layers() const { return m_printer_input; }

    void export_print(const std::string &fname, const std::string &projectname = "")
    {
        ThumbnailsList thumbnails; //empty thumbnail list
        export_print(fname, thumbnails, projectname);
    }

    void export_print(const std::string    &fname,
                      const ThumbnailsList &thumbnails,
                      const std::string    &projectname = "");
    
private:
    
    // Implement same logic as in SLAPrintObject
    bool invalidate_step(SLAPrintStep st);

    // Invalidate steps based on a set of parameters changed.
    bool invalidate_state_by_config_options(const std::vector<t_config_option_key> &opt_keys, bool &invalidate_all_model_objects);

    SLAPrintConfig                  m_print_config;
    SLAPrinterConfig                m_printer_config;
    SLAMaterialConfig               m_material_config;
    SLAPrintObjectConfig            m_default_object_config;

    PrintObjects                    m_objects;

    // Ready-made data for rasterization.
    std::vector<PrintLayer>         m_printer_input;
    
    // The archive object which collects the raster images after slicing
    std::unique_ptr<SLAArchiveWriter>     m_archiver;
    
    // Estimated print time, material consumed.
    SLAPrintStatistics              m_print_statistics;
    
    class StatusReporter
    {
        double m_st = 0;
        
    public:
        void operator()(SLAPrint &         p,
                        double             st,
                        const std::string &msg,
                        unsigned           flags = SlicingStatus::DEFAULT,
                        const std::string &logmsg = "");
        
        double status() const { return m_st; }
    } m_report_status;

	friend SLAPrintObject;
};

// Helper functions:

bool is_zero_elevation(const SLAPrintObjectConfig &c);

sla::SupportTreeConfig make_support_cfg(const SLAPrintObjectConfig& c);

sla::PadConfig::EmbedObject builtin_pad_cfg(const SLAPrintObjectConfig& c);

sla::PadConfig make_pad_cfg(const SLAPrintObjectConfig& c);

bool validate_pad(const indexed_triangle_set &pad, const sla::PadConfig &pcfg);


} // namespace Slic3r

#endif /* slic3r_SLAPrint_hpp_ */
