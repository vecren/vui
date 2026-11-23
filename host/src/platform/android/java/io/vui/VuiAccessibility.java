package io.vui;

import android.content.Context;
import android.graphics.Rect;
import android.os.Build;
import android.os.Bundle;
import android.util.SparseArray;
import android.view.InputDevice;
import android.view.MotionEvent;
import android.view.ViewParent;
import android.view.accessibility.AccessibilityEvent;
import android.view.accessibility.AccessibilityManager;
import android.view.accessibility.AccessibilityNodeInfo;
import android.view.accessibility.AccessibilityNodeProvider;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

final class VuiAccessibility extends AccessibilityNodeProvider {
    private static final int NONE = 0;
    private static final Node[] EMPTY = new Node[0];
    private static final String SLIDER = "android.widget.SeekBar";
    private static final String TEXT_FIELD = "android.widget.EditText";
    private static final int CONTENT_EVENT_INTERVAL_MS = 100;

    private static final class Identity {
        final long session, generation, mount;
        final int slot;
        int virtualId;

        Identity(long session, int slot, long generation, long mount) {
            this.session = session;
            this.slot = slot;
            this.generation = generation;
            this.mount = mount;
        }

        boolean matches(Identity other) {
            return other != null && session == other.session && slot == other.slot &&
                generation == other.generation && mount == other.mount;
        }
    }

    static final class Node {
        private final Identity key, parent;
        final String className, label, value;
        final Rect bounds;
        final int activate, forward, backward, focus, checked, scrollToPosition, rows, columns;
        final boolean collection, enabled, focused;
        final ArrayList<Integer> children = new ArrayList<>();
        int id, parentId;

        Node(long session, int slot, long generation, long mount,
             long parentSession, int parentSlot, long parentGeneration, long parentMount,
             String className, String label, String value, float left, float top, float right, float bottom,
             int activate, int forward, int backward, int focus, int checked, int scrollToPosition,
             int rows, int columns, boolean collection, boolean enabled, boolean focused) {
            key = new Identity(session, slot, generation, mount);
            parent = parentSession == 0 ? null : new Identity(parentSession, parentSlot, parentGeneration, parentMount);
            this.className = className;
            this.label = label;
            this.value = value;
            bounds = new Rect((int)Math.floor(left), (int)Math.floor(top), (int)Math.ceil(right), (int)Math.ceil(bottom));
            this.activate = activate;
            this.forward = forward;
            this.backward = backward;
            this.focus = focus;
            this.checked = checked;
            this.scrollToPosition = scrollToPosition;
            this.rows = rows;
            this.columns = columns;
            this.collection = collection;
            this.enabled = enabled;
            this.focused = focused;
        }

        boolean sameContent(Node other) {
            return id == other.id && parentId == other.parentId && bounds.equals(other.bounds) &&
                className.equals(other.className) && label.equals(other.label) && value.equals(other.value) &&
                activate == other.activate && forward == other.forward && backward == other.backward &&
                focus == other.focus && checked == other.checked && scrollToPosition == other.scrollToPosition &&
                rows == other.rows && columns == other.columns && collection == other.collection &&
                enabled == other.enabled && focused == other.focused;
        }

        boolean screenReaderFocusable() {
            return !label.isEmpty() || !value.isEmpty() || activate != NONE || focus != NONE ||
                forward != NONE || backward != NONE || scrollToPosition != NONE || checked >= 0;
        }
    }

    private final VuiView view;
    private final AccessibilityManager manager;
    private final AccessibilityManager.AccessibilityStateChangeListener stateListener = enabled -> stateChanged();
    private final AccessibilityManager.TouchExplorationStateChangeListener explorationListener = enabled -> {
        if (!enabled) updateHover(NONE);
    };
    // A slot can keep its ID while hidden. Reusing the slot with a new mount always gets a fresh ID.
    private final SparseArray<Identity> identities = new SparseArray<>();
    private final Runnable contentChanged = this::sendContentChanged;
    private State state = new State();
    private long nextId = 1;
    private int accessibilityFocus = NONE, hover = NONE;
    private boolean attached, enabled, contentPending;

    private static final class State {
        final Node[] order;
        final SparseArray<Node> nodes = new SparseArray<>();
        final ArrayList<Integer> roots = new ArrayList<>();
        int inputFocus = NONE;

        State() { this(EMPTY); }
        State(Node[] order) { this.order = order; }
    }

    VuiAccessibility(VuiView view) {
        this.view = view;
        manager = (AccessibilityManager)view.getContext().getSystemService(Context.ACCESSIBILITY_SERVICE);
    }

    void attach() {
        if (manager == null || attached) return;
        attached = true;
        manager.addAccessibilityStateChangeListener(stateListener);
        manager.addTouchExplorationStateChangeListener(explorationListener);
        stateChanged();
    }

    void detach() {
        if (!attached) return;
        attached = enabled = false;
        manager.removeAccessibilityStateChangeListener(stateListener);
        manager.removeTouchExplorationStateChangeListener(explorationListener);
        reset();
        view.removeCallbacks(contentChanged);
        contentPending = false;
    }

    boolean enabled() { return enabled; }

    private void stateChanged() {
        enabled = attached && manager.isEnabled();
        if (enabled()) view.schedulePump();
        else reset();
    }

    void reset() { if (state.order.length != 0) publish(EMPTY); }

    void publish(Node[] nodes) {
        if (nodes == null) return;
        State old = state;
        State next = new State(nodes);
        boolean changed = nodes.length != old.order.length;
        for (Node node : nodes) {
            Identity identity = identities.get(node.key.slot);
            if (!node.key.matches(identity)) {
                if (nextId >= Integer.MAX_VALUE) continue;
                identity = node.key;
                identity.virtualId = (int)nextId++;
                identities.put(identity.slot, identity);
            }
            node.id = identity.virtualId;
            next.nodes.put(node.id, node);
            if (node.focused) next.inputFocus = node.id;
        }
        for (int index = 0; index < nodes.length; ++index) {
            Node node = nodes[index];
            if (node.id == NONE) continue;
            Identity parent = node.parent == null ? null : identities.get(node.parent.slot);
            Node parentNode = node.parent != null && node.parent.matches(parent) ? next.nodes.get(parent.virtualId) : null;
            node.parentId = parentNode == null ? HOST_VIEW_ID : parentNode.id;
            if (parentNode == null) next.roots.add(node.id);
            else parentNode.children.add(node.id);
            if (!changed && !node.sameContent(old.order[index])) changed = true;
        }
        state = next;
        if (accessibilityFocus != NONE && next.nodes.get(accessibilityFocus) == null) {
            int previous = accessibilityFocus;
            accessibilityFocus = NONE;
            send(previous, old.nodes.get(previous), AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
        }
        if (hover != NONE && next.nodes.get(hover) == null) {
            int previous = hover;
            hover = NONE;
            send(previous, old.nodes.get(previous), AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
        }
        if (next.inputFocus != NONE && next.inputFocus != old.inputFocus)
            send(next.inputFocus, next.nodes.get(next.inputFocus), AccessibilityEvent.TYPE_VIEW_FOCUSED);
        if (changed && enabled()) {
            if (old.order.length == 0 || next.order.length == 0) {
                view.removeCallbacks(contentChanged);
                sendContentChanged();
            } else if (!contentPending) {
                contentPending = true;
                view.postDelayed(contentChanged, CONTENT_EVENT_INTERVAL_MS);
            }
        }
    }

    @Override public AccessibilityNodeInfo createAccessibilityNodeInfo(int id) {
        if (!enabled()) return null;
        if (id == HOST_VIEW_ID) {
            AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(view);
            view.onInitializeAccessibilityNodeInfo(info);
            if (state.inputFocus != NONE) info.setFocused(false);
            for (int child : state.roots) info.addChild(view, child);
            return info;
        }
        Node node = state.nodes.get(id);
        if (node == null) return null;
        AccessibilityNodeInfo info = AccessibilityNodeInfo.obtain(view, id);
        info.setParent(view, node.parentId);
        for (int child : node.children) info.addChild(view, child);
        info.setPackageName(view.getContext().getPackageName());
        info.setClassName(node.className);
        if (TEXT_FIELD.equals(node.className)) {
            info.setText(node.value);
            if (!node.label.isEmpty()) info.setContentDescription(node.label);
            info.setEditable(true);
        } else {
            if (!node.label.isEmpty()) info.setText(node.label);
            if (!node.value.isEmpty()) {
                if (Build.VERSION.SDK_INT >= 30) info.setStateDescription(node.value);
                else info.setContentDescription(node.label.isEmpty() ? node.value : node.label + ", " + node.value);
            }
        }
        if (node.checked >= 0) {
            info.setCheckable(true);
            info.setChecked(node.checked != 0);
        }
        info.setEnabled(node.enabled);
        info.setScreenReaderFocusable(node.screenReaderFocusable());
        info.setFocusable(node.focus != NONE || node.focused);
        info.setFocused(node.focused);
        info.setAccessibilityFocused(id == accessibilityFocus);
        info.setClickable(node.activate != NONE);
        info.setScrollable(!SLIDER.equals(node.className) && (node.forward != NONE || node.backward != NONE));
        if (node.collection)
            info.setCollectionInfo(AccessibilityNodeInfo.CollectionInfo.obtain(node.rows, node.columns, false));
        if (node.enabled) {
            if (node.activate != NONE) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLICK);
            if (node.forward != NONE) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_FORWARD);
            if (node.backward != NONE) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_BACKWARD);
            if (node.scrollToPosition != NONE)
                info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_TO_POSITION);
            if (node.focus != NONE && !node.focused) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_FOCUS);
        }
        if (id == accessibilityFocus) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_CLEAR_ACCESSIBILITY_FOCUS);
        else if (node.screenReaderFocusable()) info.addAction(AccessibilityNodeInfo.AccessibilityAction.ACTION_ACCESSIBILITY_FOCUS);
        setBounds(info, node);
        return info;
    }

    private void setBounds(AccessibilityNodeInfo info, Node node) {
        Rect bounds = new Rect(node.bounds);
        Node parent = state.nodes.get(node.parentId);
        if (parent != null) bounds.offset(-parent.bounds.left, -parent.bounds.top);
        info.setBoundsInParent(bounds);
        bounds.set(node.bounds);
        Rect visible = new Rect();
        info.setVisibleToUser(view.isShown() && view.getLocalVisibleRect(visible) && bounds.intersect(visible));
        int[] origin = new int[2];
        view.getLocationOnScreen(origin);
        bounds.offset(origin[0], origin[1]);
        info.setBoundsInScreen(bounds);
    }

    @Override public AccessibilityNodeInfo findFocus(int focus) {
        if (focus == AccessibilityNodeInfo.FOCUS_ACCESSIBILITY) return createAccessibilityNodeInfo(accessibilityFocus);
        if (focus == AccessibilityNodeInfo.FOCUS_INPUT) return createAccessibilityNodeInfo(state.inputFocus);
        return null;
    }

    @Override public List<AccessibilityNodeInfo> findAccessibilityNodeInfosByText(String text, int id) {
        ArrayList<AccessibilityNodeInfo> result = new ArrayList<>();
        if (!enabled() || text == null || text.isEmpty()) return result;
        String query = text.toLowerCase(Locale.ROOT);
        for (Node node : state.order) {
            if (node.id == NONE || !(node.label.toLowerCase(Locale.ROOT).contains(query) ||
                node.value.toLowerCase(Locale.ROOT).contains(query))) continue;
            Node ancestor = node;
            while (id != HOST_VIEW_ID && ancestor != null && ancestor.id != id)
                ancestor = state.nodes.get(ancestor.parentId);
            if (id == HOST_VIEW_ID || ancestor != null) result.add(createAccessibilityNodeInfo(node.id));
        }
        return result;
    }

    @Override public boolean performAction(int id, int action, Bundle arguments) {
        if (!enabled()) return false;
        if (id == HOST_VIEW_ID) return view.performAccessibilityAction(action, arguments);
        Node node = state.nodes.get(id);
        if (node == null) return false;
        if (action == AccessibilityNodeInfo.ACTION_CLEAR_ACCESSIBILITY_FOCUS) {
            if (accessibilityFocus != id) return false;
            accessibilityFocus = NONE;
            send(id, node, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
            return true;
        }
        if (action == AccessibilityNodeInfo.ACTION_ACCESSIBILITY_FOCUS) {
            if (accessibilityFocus == id || !node.screenReaderFocusable()) return false;
            int previous = accessibilityFocus;
            accessibilityFocus = id;
            send(previous, state.nodes.get(previous), AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUS_CLEARED);
            send(id, node, AccessibilityEvent.TYPE_VIEW_ACCESSIBILITY_FOCUSED);
            return true;
        }
        if (!node.enabled) return false;
        int nativeAction, row = -1, column = -1;
        boolean scrollToPosition = action == AccessibilityNodeInfo.AccessibilityAction.ACTION_SCROLL_TO_POSITION.getId();
        if (scrollToPosition) {
            if (arguments == null || node.scrollToPosition == NONE) return false;
            row = arguments.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_ROW_INT, 0);
            column = arguments.getInt(AccessibilityNodeInfo.ACTION_ARGUMENT_COLUMN_INT, 0);
            if (row < 0 || column < 0) return false;
            nativeAction = node.scrollToPosition;
        } else switch (action) {
            case AccessibilityNodeInfo.ACTION_CLICK: nativeAction = node.activate; break;
            case AccessibilityNodeInfo.ACTION_SCROLL_FORWARD: nativeAction = node.forward; break;
            case AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD: nativeAction = node.backward; break;
            case AccessibilityNodeInfo.ACTION_FOCUS:
                if (node.focus == NONE || node.focused || !view.requestFocus()) return false;
                nativeAction = node.focus;
                break;
            default: return false;
        }
        Identity key = node.key;
        if (nativeAction == NONE ||
            !view.performSemantics(key.session, key.slot, key.generation, key.mount, nativeAction, row, column))
            return false;
        if (action == AccessibilityNodeInfo.ACTION_CLICK)
            send(id, node, AccessibilityEvent.TYPE_VIEW_CLICKED);
        else if (action == AccessibilityNodeInfo.ACTION_SCROLL_FORWARD ||
                 action == AccessibilityNodeInfo.ACTION_SCROLL_BACKWARD || scrollToPosition)
            send(id, node, SLIDER.equals(node.className) ? AccessibilityEvent.TYPE_VIEW_SELECTED : AccessibilityEvent.TYPE_VIEW_SCROLLED);
        return true;
    }

    boolean hover(MotionEvent event) {
        if (!enabled() || !manager.isTouchExplorationEnabled() || !event.isFromSource(InputDevice.SOURCE_TOUCHSCREEN))
            return false;
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_HOVER_ENTER:
            case MotionEvent.ACTION_HOVER_MOVE:
                int id = NONE;
                for (int i = state.order.length - 1; i >= 0; --i) {
                    Node node = state.order[i];
                    if (node.id != NONE && node.screenReaderFocusable() &&
                        node.bounds.contains((int)Math.floor(event.getX()), (int)Math.floor(event.getY()))) {
                        id = node.id;
                        break;
                    }
                }
                updateHover(id);
                return true;
            case MotionEvent.ACTION_HOVER_EXIT:
                updateHover(NONE);
                return true;
            default: return false;
        }
    }

    private void updateHover(int id) {
        if (hover == id) return;
        int previous = hover;
        hover = id;
        send(id, state.nodes.get(id), AccessibilityEvent.TYPE_VIEW_HOVER_ENTER);
        send(previous, state.nodes.get(previous), AccessibilityEvent.TYPE_VIEW_HOVER_EXIT);
    }

    private void sendContentChanged() {
        contentPending = false;
        send(HOST_VIEW_ID, null, AccessibilityEvent.TYPE_WINDOW_CONTENT_CHANGED);
    }

    private void send(int id, Node node, int type) {
        if (!enabled() || id == NONE) return;
        ViewParent parent = view.getParent();
        if (parent == null) return;
        AccessibilityEvent event = AccessibilityEvent.obtain(type);
        event.setSource(view, id);
        event.setPackageName(view.getContext().getPackageName());
        if (node != null) {
            event.setClassName(node.className);
            event.setEnabled(node.enabled);
            event.setChecked(node.checked > 0);
            if (!node.label.isEmpty()) event.setContentDescription(node.label);
            if (!node.value.isEmpty()) event.getText().add(node.value);
        } else {
            event.setClassName(view.getAccessibilityClassName());
            event.setContentChangeTypes(AccessibilityEvent.CONTENT_CHANGE_TYPE_SUBTREE);
        }
        parent.requestSendAccessibilityEvent(view, event);
    }
}
