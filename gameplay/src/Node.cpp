#include "Base.h"
#include "Node.h"
#include "Scene.h"
#include "Joint.h"

#define NODE_DIRTY_WORLD 1
#define NODE_DIRTY_BOUNDS 2
#define NODE_DIRTY_ALL (NODE_DIRTY_WORLD | NODE_DIRTY_BOUNDS)

namespace gameplay
{

Node::Node(const char* id)
    : _scene(NULL), _firstChild(NULL), _nextSibling(NULL), _prevSibling(NULL), _parent(NULL), _childCount(NULL),
    _camera(NULL), _light(NULL), _model(NULL), _audioSource(NULL), _particleEmitter(NULL), _physicsRigidBody(NULL), 
    _dirtyBits(NODE_DIRTY_ALL), _notifyHierarchyChanged(true)
{
    if (id)
    {
        _id = id;
    }
}

Node::Node(const Node& node)
{
    // hidden
}

Node::~Node()
{
    removeAllChildren();

    SAFE_RELEASE(_camera);
    SAFE_RELEASE(_light);
    SAFE_RELEASE(_model);
    SAFE_RELEASE(_audioSource);
    SAFE_RELEASE(_particleEmitter);
    SAFE_DELETE(_physicsRigidBody);
}

Node* Node::create(const char* id)
{
    return new Node(id);
}

const char* Node::getId() const
{
    return _id.c_str();
}

void Node::setId(const char* id)
{
    if (id)
    {
        _id = id;
    }
}

Node::Type Node::getType() const
{
    return Node::NODE;
}

void Node::addChild(Node* child)
{
    assert(child);

    if (child->_parent == this)
    {
        // This node is already present in our hierarchy
        return;
    }

    child->addRef();

    // If the item belongs to another hierarchy, remove it first.
    if (child->_parent)
    {
        child->_parent->removeChild(child);
    }
    else if (child->_scene)
    {
        child->_scene->removeNode(child);
    }

    // Order is irrelevant, so add to the beginning of the list.
    if (_firstChild)
    {
        _firstChild->_prevSibling = child;
        child->_nextSibling = _firstChild;
        _firstChild = child;
    }
    else
    {
        _firstChild = child;
    }

    child->_parent = this;

    ++_childCount;

    if (_notifyHierarchyChanged)
    {
        hierarchyChanged();
    }
}

void Node::removeChild(Node* child)
{
    if (child == NULL || child->_parent != this)
    {
        // The child is not in our hierarchy.
        return;
    }

    // Call remove on the child.
    child->remove();

    SAFE_RELEASE(child);
}

void Node::removeAllChildren()
{
    _notifyHierarchyChanged = false;

    while (_firstChild)
    {
        removeChild(_firstChild);
    }

    _notifyHierarchyChanged = true;
    hierarchyChanged();
}

void Node::remove()
{
    // Re-link our neighbours.
    if (_prevSibling)
    {
        _prevSibling->_nextSibling = _nextSibling;
    }
    if (_nextSibling)
    {
        _nextSibling->_prevSibling = _prevSibling;
    }

    // Update our parent.
    Node* parent = _parent;
    if (parent)
    {
        if (this == parent->_firstChild)
        {
            parent->_firstChild = _nextSibling;
        }

        --parent->_childCount;
    }

    _nextSibling = NULL;
    _prevSibling = NULL;
    _parent = NULL;

    if (parent && parent->_notifyHierarchyChanged)
    {
        parent->hierarchyChanged();
    }
}

Node* Node::getFirstChild() const
{
    return _firstChild;
}

Node* Node::getNextSibling() const
{
    return _nextSibling;
}

Node* Node::getPreviousSibling() const
{
    return _prevSibling;
}

Node* Node::getParent() const
{
    return _parent;
}

unsigned int Node::getChildCount() const
{
    return _childCount;
}

Node* Node::findNode(const char* id, bool recursive, bool exactMatch)
{
    assert(id);
    
    // Search immediate children first.
    for (Node* child = getFirstChild(); child != NULL; child = child->getNextSibling())
    {
        // Does this child's ID match?
        if ((exactMatch && child->_id == id) || (!exactMatch && child->_id.find(id) == 0))
        {
            return child;
        }
    }

    // Recurse.
    if (recursive)
    {
        for (Node* child = getFirstChild(); child != NULL; child = child->getNextSibling())
        {
            Node* match = child->findNode(id, true, exactMatch);
            if (match)
            {
                return match;
            }
        }
    }

    return NULL;
}   

unsigned int Node::findNodes(const char* id, std::vector<Node*>& nodes, bool recursive, bool exactMatch)
{
    assert(id);
    
    unsigned int count = 0;

    // Search immediate children first.
    for (Node* child = getFirstChild(); child != NULL; child = child->getNextSibling())
    {
        // Does this child's ID match?
        if ((exactMatch && child->_id == id) || (!exactMatch && child->_id.find(id) == 0))
        {
            nodes.push_back(child);
            ++count;
        }
    }

    // Recurse.
    if (recursive)
    {
        for (Node* child = getFirstChild(); child != NULL; child = child->getNextSibling())
        {
            count += child->findNodes(id, nodes, true, exactMatch);
        }
    }

    return count;
}

Scene* Node::getScene() const
{
    // Search for a scene in our parents.
    for (Node* n = const_cast<Node*>(this); n != NULL; n = n->getParent())
    {
        if (n->_scene)
        {
            return n->_scene;
        }
    }

    return NULL;
}

Node* Node::getRootNode() const
{
    Node* n = const_cast<Node*>(this);
    while (n->getParent())
    {
        n = n->getParent();
    }
    return n;
}

const Matrix& Node::getWorldMatrix() const
{
    if (_dirtyBits & NODE_DIRTY_WORLD)
    {
        // Clear our dirty flag immediately to prevent this block from being entered if our
        // parent calls our getWorldMatrix() method as a result of the following calculations.
        _dirtyBits &= ~NODE_DIRTY_WORLD;

        // If we have a parent, multiply our parent world transform by our local
        // transform to obtain our final resolved world transform.
        Node* parent = getParent();
        if (parent && (!_physicsRigidBody || _physicsRigidBody->isKinematic()) )
        {
            Matrix::multiply(parent->getWorldMatrix(), getMatrix(), &_world);
        }
        else
        {
            _world = getMatrix();
        }

        // Our world matrix was just updated, so call getWorldMatrix() on all child nodes
        // to force their resolved world matrices to be updated.
        Node* node = getFirstChild();
        while (node)
        {
            node->getWorldMatrix();
            node = node->getNextSibling();
        }
    }

    return _world;
}

const Matrix& Node::getWorldViewMatrix() const
{
    static Matrix worldView;
    
    Matrix::multiply(getViewMatrix(), getWorldMatrix(), &worldView);

    return worldView;
}

const Matrix& Node::getInverseTransposeWorldViewMatrix() const
{
    static Matrix invTransWorldView;

    // Assume the matrix is always dirty since the camera is moving
    // almost every frame in most games.
    //    
    // TODO: Optimize here to NOT calculate the inverse transpose if the matrix is orthogonal.
    Matrix::multiply(getViewMatrix(), getWorldMatrix(), &invTransWorldView);
    invTransWorldView.invert();
    invTransWorldView.transpose();

    return invTransWorldView;
}

const Matrix& Node::getViewMatrix() const
{
    Scene* scene = getScene();
    Camera* camera = scene ? scene->getActiveCamera() : NULL;
    if (camera)
    {
        return camera->getViewMatrix();
    }
    else
    {
        return Matrix::identity();
    }
}

const Matrix& Node::getInverseViewMatrix() const
{
    Scene* scene = getScene();
    Camera* camera = scene ? scene->getActiveCamera() : NULL;
    if (camera)
    {
        return camera->getInverseViewMatrix();
    }
    else
    {
        return Matrix::identity();
    }
}

const Matrix& Node::getProjectionMatrix() const
{
    Scene* scene = getScene();
    Camera* camera = scene ? scene->getActiveCamera() : NULL;
    if (camera)
    {
        return camera->getProjectionMatrix();
    }
    else
    {
        return Matrix::identity();
    }
}

const Matrix& Node::getViewProjectionMatrix() const
{
    Scene* scene = getScene();
    Camera* camera = scene ? scene->getActiveCamera() : NULL;
    if (camera)
    {
        return camera->getViewProjectionMatrix();
    }
    else
    {
        return Matrix::identity();
    }
}

const Matrix& Node::getInverseViewProjectionMatrix() const
{
    Scene* scene = getScene();
    Camera* camera = scene ? scene->getActiveCamera() : NULL;
    if (camera)
    {
        return camera->getInverseViewProjectionMatrix();
    }

    return Matrix::identity();
}

const Matrix& Node::getWorldViewProjectionMatrix() const
{
    static Matrix worldViewProj;

    // Always re-calculate worldViewProjection matrix since it's extremely difficult
    // to track whether the camera has changed (it may frequently change every frame).
    Matrix::multiply(getViewProjectionMatrix(), getWorldMatrix(), &worldViewProj);

    return worldViewProj;
}

Vector3 Node::getTranslationWorld() const
{
    Vector3 translation;
    getWorldMatrix().getTranslation(&translation);
    return translation;
}

Vector3 Node::getTranslationView() const
{
    Vector3 translation;
    getWorldMatrix().getTranslation(&translation);
    getViewMatrix().transformPoint(&translation);
    return translation;
}

Vector3 Node::getForwardVectorWorld() const
{
    Vector3 vector;
    getWorldMatrix().getForwardVector(&vector);
    return vector;
}

Vector3 Node::getForwardVectorView() const
{
    Vector3 vector;
    getWorldMatrix().getForwardVector(&vector);
    getViewMatrix().transformVector(&vector);
    return vector;
}

Vector3 Node::getActiveCameraTranslationWorld() const
{
    Scene* scene = getScene();
    if (scene)
    {
        Camera* camera = scene->getActiveCamera();
        if (camera)
        {
            Node* cameraNode = camera->getNode();
            if (cameraNode)
            {
                return cameraNode->getTranslationWorld();
            }
        }
    }

    return Vector3::zero();
}

void Node::hierarchyChanged()
{
    // When our hierarchy changes our world transform is affected, so we must dirty it.
    transformChanged();
}

void Node::transformChanged()
{
    // Our local transform was changed, so mark our world matrices dirty.
    _dirtyBits |= NODE_DIRTY_WORLD | NODE_DIRTY_BOUNDS;

    // Notify our children that their transform has also changed (since transforms are inherited).
    Joint* rootJoint = NULL;
    Node* n = getFirstChild();
    while (n)
    {
        n->transformChanged();
        n = n->getNextSibling();
    }

    Transform::transformChanged();
}

void Node::setBoundsDirty()
{
    // Mark ourself and our parent nodes as dirty
    _dirtyBits |= NODE_DIRTY_BOUNDS;

    // Mark our parent bounds as dirty as well
    if (_parent)
        _parent->setBoundsDirty();
}

Camera* Node::getCamera() const
{
    return _camera;
}

void Node::setCamera(Camera* camera)
{
    if (_camera != camera)
    {
        if (_camera)
        {
            _camera->setNode(NULL);
            SAFE_RELEASE(_camera);
        }

        _camera = camera;

        if (_camera)
        {
            _camera->addRef();
            _camera->setNode(this);
        }
    }
}

Light* Node::getLight() const
{
    return _light;
}

void Node::setLight(Light* light)
{
    if (_light != light)
    {
        if (_light)
        {
            _light->setNode(NULL);
            SAFE_RELEASE(_light);
        }

        _light = light;

        if (_light)
        {
            _light->addRef();
            _light->setNode(this);
        }
    }
}

void Node::setModel(Model* model)
{
    if (_model != model)
    {
        if (_model)
        {
            _model->setNode(NULL);
            SAFE_RELEASE(_model);
        }

        _model = model;

        if (_model)
        {
            _model->addRef();
            _model->setNode(this);
        }
    }
}

Model* Node::getModel() const
{
    return _model;
}

const BoundingSphere& Node::getBoundingSphere() const
{
    if (_dirtyBits & NODE_DIRTY_BOUNDS)
    {
        _dirtyBits &= ~NODE_DIRTY_BOUNDS;

        const Matrix& worldMatrix = getWorldMatrix();

        // Start with our local bounding sphere
        // TODO: Incorporate bounds from entities other than mesh (i.e. emitters, audiosource, etc)
        bool empty = true;
        if (_model && _model->getMesh())
        {
            _bounds.set(_model->getMesh()->getBoundingSphere());
            empty = false;
        }
        else
        {
            // Empty bounding sphere, set the world translation with zero radius
            worldMatrix.getTranslation(&_bounds.center);
            _bounds.radius = 0;
        }

        // Transform the sphere (if not empty) into world space.
        if (!empty)
        {
            bool applyWorldTransform = true;
            if (_model && _model->getSkin())
            {
                // Special case: If the root joint of our mesh skin is parented by any nodes, 
                // multiply the world matrix of the root joint's parent by this node's
                // world matrix. This computes a final world matrix used for transforming this
                // node's bounding volume. This allows us to store a much smaller bounding
                // volume approximation than would otherwise be possible for skinned meshes,
                // since joint parent nodes that are not in the matrix pallette do not need to
                // be considered as directly transforming vertices on the GPU (they can instead
                // be applied directly to the bounding volume transformation below).
                Node* jointParent = _model->getSkin()->getRootJoint()->getParent();
                if (jointParent)
                {
                    // TODO: Should we protect against the case where joints are nested directly
                    // in the node hierachy of the model (this is normally not the case)?
                    Matrix boundsMatrix;
                    Matrix::multiply(getWorldMatrix(), jointParent->getWorldMatrix(), &boundsMatrix);
                    _bounds.transform(boundsMatrix);
                    applyWorldTransform = false;
                }
            }
            if (applyWorldTransform)
            {
                _bounds.transform(getWorldMatrix());
            }
        }

        // Merge this world-space bounding sphere with our childrens' bounding volumes.
        for (Node* n = getFirstChild(); n != NULL; n = n->getNextSibling())
        {
            const BoundingSphere& childSphere = n->getBoundingSphere();
            if (!childSphere.isEmpty())
            {
                if (empty)
                {
                    _bounds.set(childSphere);
                    empty = false;
                }
                else
                {
                    _bounds.merge(childSphere);
                }
            }
        }
    }

    return _bounds;
}

AudioSource* Node::getAudioSource() const
{
    return _audioSource;
}

void Node::setAudioSource(AudioSource* audio)
{
    if (_audioSource != audio)
    {
        if (_audioSource)
        {
            _audioSource->setNode(NULL);
            SAFE_RELEASE(_audioSource);
        }
        
        _audioSource = audio;

        if (_audioSource)
        {
            _audioSource->addRef();
            _audioSource->setNode(this);
        }
    }
}

ParticleEmitter* Node::getParticleEmitter() const
{
    return _particleEmitter;
}

void Node::setParticleEmitter(ParticleEmitter* emitter)
{
    if (_particleEmitter != emitter)
    {
        if (_particleEmitter)
        {
            _particleEmitter->setNode(NULL);
            SAFE_RELEASE(_particleEmitter);
        }
        
        _particleEmitter = emitter;

        if (_particleEmitter)
        {
            _particleEmitter->addRef();
            _particleEmitter->setNode(this);
        }
    }
}

PhysicsRigidBody* Node::getPhysicsRigidBody() const
{
    return _physicsRigidBody;
}

void Node::setPhysicsRigidBody(PhysicsRigidBody::Type type, float mass, float friction,
        float restitution, float linearDamping, float angularDamping)
{
    SAFE_DELETE(_physicsRigidBody);
    
    if (type != PhysicsRigidBody::SHAPE_NONE)
        _physicsRigidBody = new PhysicsRigidBody(this, type, mass, friction, restitution, linearDamping, angularDamping);
}

void Node::setPhysicsRigidBody(const char* filePath)
{
    SAFE_DELETE(_physicsRigidBody);

    _physicsRigidBody = PhysicsRigidBody::create(this, filePath);
}

void Node::setPhysicsRigidBody(Properties* properties)
{
    SAFE_DELETE(_physicsRigidBody);

    _physicsRigidBody = PhysicsRigidBody::create(this, properties);
}

}
