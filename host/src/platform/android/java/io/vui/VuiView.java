package io.vui;

import android.app.Activity;
import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.os.Build;
import android.os.Bundle;
import android.text.Editable;
import android.text.Selection;
import android.text.SpannableStringBuilder;
import android.text.InputType;
import android.view.Choreographer;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.WindowInsets;
import android.view.WindowInsetsAnimation;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.List;

public final class VuiView extends SurfaceView implements SurfaceHolder.Callback2, Choreographer.FrameCallback {
    static { System.loadLibrary("vui_android"); }

    private final Handler handler = new Handler(Looper.getMainLooper());
    private final AtomicBoolean posted = new AtomicBoolean();
    private final TimelineCallback timeline = Build.VERSION.SDK_INT >= 33 ? new TimelineCallback(this) : null;
    private final Runnable advance = () -> {
        posted.set(false);
        pump();
    };
    private long handle;
    private long period = 16_666_667;
    private boolean framePending, surfaceAvailable;
    private long textSession, textRevision;
    private EditorConnection editor;
    private boolean applyingText, imeVisible;

    public VuiView(Context context) {
        super(context);
        setFocusable(true);
        setFocusableInTouchMode(true);
        getHolder().addCallback(this);
        handle = nativeCreate(this);
        if (handle == 0) throw new IllegalStateException("Vui application creation failed");
        setOnApplyWindowInsetsListener((view, insets) -> {
            if (handle == 0) return insets;
            long session = textSession;
            boolean visible = updateMetrics(insets);
            boolean dismissed = imeVisible && !visible && session != 0;
            imeVisible = visible;
            if (dismissed) nativeImeDismissed(handle, session);
            schedulePump();
            return insets;
        });
        if (Build.VERSION.SDK_INT >= 30) {
            setWindowInsetsAnimationCallback(new WindowInsetsAnimation.Callback(WindowInsetsAnimation.Callback.DISPATCH_MODE_CONTINUE_ON_SUBTREE) {
                @Override public WindowInsets onProgress(WindowInsets insets, List<WindowInsetsAnimation> animations) {
                    updateMetrics(insets);
                    schedulePump();
                    return insets;
                }
            });
        }
        requestFocus();
    }

    public void schedulePump() {
        if (posted.compareAndSet(false, true)) handler.post(advance);
    }

    private void pump() {
        long now = System.nanoTime();
        pump(now, now, 0);
    }

    private void pump(long now, long sample, long expected) {
        if (handle == 0) return;
        handler.removeCallbacks(advance);
        posted.set(false);
        int wait = nativePump(handle, now, sample, expected);
        if (getContext() instanceof VuiActivity) ((VuiActivity)getContext()).setBackEnabled(nativeCanBack(handle));
        if (wait < 0) return;
        if (wait <= 16 && surfaceAvailable) {
            if (!framePending) {
                framePending = true;
                if (timeline != null) timeline.post();
                else Choreographer.getInstance().postFrameCallback(this);
            }
        } else if (wait < 1000) {
            if (posted.compareAndSet(false, true)) handler.postDelayed(advance, Math.max(1, wait));
        }
    }

    @Override
    public void doFrame(long time) {
        pumpFrame(time, time + period);
    }

    private void pumpFrame(long sample, long expected) {
        framePending = false;
        pump(System.nanoTime(), sample, expected);
    }

    private void cancelFrame() {
        if (timeline != null) timeline.remove();
        else Choreographer.getInstance().removeFrameCallback(this);
        framePending = false;
    }

    private static final class TimelineCallback implements Choreographer.VsyncCallback {
        private final VuiView view;

        TimelineCallback(VuiView view) { this.view = view; }

        void post() { Choreographer.getInstance().postVsyncCallback(this); }
        void remove() { Choreographer.getInstance().removeVsyncCallback(this); }

        @Override
        public void onVsync(Choreographer.FrameData frame) {
            view.pumpFrame(frame.getFrameTimeNanos(), frame.getPreferredFrameTimeline().getExpectedPresentationTimeNanos());
        }
    }

    public void setActive(boolean active) {
        if (handle != 0) {
            if (!active) cancelFrame();
            nativeActive(handle, active);
            schedulePump();
        }
    }

    public boolean back() { return handle != 0 && nativeBack(handle); }

    public void close() {
        handler.removeCallbacksAndMessages(null);
        cancelFrame();
        posted.set(false);
        if (handle != 0) {
            long previous = handle;
            handle = 0;
            nativeDestroy(previous);
        }
        getHolder().removeCallback(this);
        if (Build.VERSION.SDK_INT >= 30) setWindowInsetsAnimationCallback(null);
    }

    @Override
    public void surfaceCreated(SurfaceHolder holder) {
        if (handle == 0) return;
        surfaceAvailable = nativeSurface(handle, holder.getSurface());
        updateMetrics(getRootWindowInsets());
        schedulePump();
    }

    @Override
    public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
        if (handle == 0) return;
        nativeResize(handle, width, height);
        updateMetrics(getRootWindowInsets());
        schedulePump();
    }

    @Override
    public void surfaceDestroyed(SurfaceHolder holder) {
        surfaceAvailable = false;
        if (handle != 0) nativeSurface(handle, null);
        cancelFrame();
    }

    @Override
    public void surfaceRedrawNeeded(SurfaceHolder holder) {
        if (handle != 0) nativeRedraw(handle);
        pump();
    }

    @Override
    public void surfaceRedrawNeededAsync(SurfaceHolder holder, Runnable done) {
        if (handle == 0 || !surfaceAvailable) {
            done.run();
            return;
        }
        nativeRedrawAsync(handle, done);
        pump();
    }

    private boolean updateMetrics(WindowInsets insets) {
        if (handle == 0) return false;
        android.graphics.Insets safe = android.graphics.Insets.NONE;
        android.graphics.Insets ime = android.graphics.Insets.NONE;
        if (insets != null && Build.VERSION.SDK_INT >= 30) {
            safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            ime = insets.getInsets(WindowInsets.Type.ime());
        } else if (insets != null) {
            safe = android.graphics.Insets.of(insets.getStableInsetLeft(), insets.getStableInsetTop(),
                insets.getStableInsetRight(), insets.getStableInsetBottom());
            android.view.DisplayCutout cutout = insets.getDisplayCutout();
            if (cutout != null)
                safe = android.graphics.Insets.max(safe, android.graphics.Insets.of(cutout.getSafeInsetLeft(),
                    cutout.getSafeInsetTop(), cutout.getSafeInsetRight(), cutout.getSafeInsetBottom()));
            android.graphics.Rect visible = new android.graphics.Rect();
            getWindowVisibleDisplayFrame(visible);
            int keyboard = Math.max(0, getRootView().getHeight() - visible.bottom - safe.bottom);
            ime = android.graphics.Insets.of(0, 0, 0, keyboard);
        }
        float refresh = getDisplay() == null ? 60 : getDisplay().getRefreshRate();
        period = (long)(1_000_000_000.0 / Math.max(1, refresh));
        nativeMetrics(handle, getResources().getDisplayMetrics().density,
            getResources().getConfiguration().fontScale, refresh,
            safe.left, safe.top, safe.right, safe.bottom, ime.left, ime.top, ime.right, ime.bottom);
        return insets != null && Build.VERSION.SDK_INT >= 30 ? insets.isVisible(WindowInsets.Type.ime()) : ime.bottom > 0;
    }

    private void pointer(MotionEvent event, int index, int action, boolean history) {
        int count = history ? event.getHistorySize() : 0;
        long[] times = new long[count];
        float[] points = new float[count * 3];
        for (int i = 0; i < count; ++i) {
            times[i] = event.getHistoricalEventTime(i) * 1_000_000;
            points[i * 3] = event.getHistoricalX(index, i);
            points[i * 3 + 1] = event.getHistoricalY(index, i);
            points[i * 3 + 2] = event.getHistoricalPressure(index, i);
        }
        nativePointer(handle, action, event.getPointerId(index), event.getToolType(index),
            event.getDeviceId(), action == MotionEvent.ACTION_BUTTON_PRESS || action == MotionEvent.ACTION_BUTTON_RELEASE ?
                event.getActionButton() : event.getButtonState(), event.getMetaState(), event.getEventTime() * 1_000_000,
            event.getX(index), event.getY(index), event.getPressure(index), times, points);
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (handle == 0) return false;
        requestUnbufferedDispatch(event);
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN ||
            action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
                for (int i = 0; i < event.getPointerCount(); ++i)
                    if (i != event.getActionIndex()) pointer(event, i, MotionEvent.ACTION_MOVE, false);
            }
            pointer(event, event.getActionIndex(), action, false);
        } else {
            for (int i = 0; i < event.getPointerCount(); ++i) pointer(event, i, action, true);
        }
        if (action == MotionEvent.ACTION_DOWN) requestFocus();
        pump();
        return true;
    }

    @Override
    public boolean onGenericMotionEvent(MotionEvent event) {
        if (handle == 0) return false;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_SCROLL) {
            nativeWheel(handle, event.getDeviceId(), event.getX(), event.getY(),
                event.getAxisValue(MotionEvent.AXIS_HSCROLL), event.getAxisValue(MotionEvent.AXIS_VSCROLL),
                event.getEventTime() * 1_000_000, event.getMetaState());
        } else if (action == MotionEvent.ACTION_HOVER_ENTER || action == MotionEvent.ACTION_HOVER_MOVE ||
                   action == MotionEvent.ACTION_HOVER_EXIT || action == MotionEvent.ACTION_BUTTON_PRESS ||
                   action == MotionEvent.ACTION_BUTTON_RELEASE) {
            pointer(event, event.getActionIndex(), action, true);
        } else return super.onGenericMotionEvent(event);
        pump();
        return true;
    }

    @Override
    public boolean onKeyDown(int code, KeyEvent event) {
        if (handle == 0 || code == KeyEvent.KEYCODE_BACK) return super.onKeyDown(code, event);
        boolean accepted = nativeKey(handle, code, true, event.getRepeatCount() != 0,
            event.getMetaState(), event.getDeviceId(), event.getEventTime() * 1_000_000);
        int character = event.getUnicodeChar();
        if (textSession != 0 && character >= 32 && character <= Character.MAX_CODE_POINT &&
            !event.isCtrlPressed() && !event.isAltPressed()) {
            if (editor == null) onCreateInputConnection(new EditorInfo());
            editor.commitText(new String(Character.toChars(character)), 1);
            accepted = true;
        }
        pump();
        return accepted || super.onKeyDown(code, event);
    }

    @Override
    public boolean onKeyUp(int code, KeyEvent event) {
        if (handle == 0 || code == KeyEvent.KEYCODE_BACK) return super.onKeyUp(code, event);
        boolean accepted = nativeKey(handle, code, false, false, event.getMetaState(),
            event.getDeviceId(), event.getEventTime() * 1_000_000);
        pump();
        return accepted || super.onKeyUp(code, event);
    }

    @Override
    protected void onFocusChanged(boolean focused, int direction, android.graphics.Rect previous) {
        super.onFocusChanged(focused, direction, previous);
        if (handle != 0) {
            nativeFocus(handle, focused);
            schedulePump();
        }
    }

    @Override
    public void onWindowFocusChanged(boolean focused) {
        super.onWindowFocusChanged(focused);
        if (handle != 0) {
            nativeWindowFocus(handle, focused);
            schedulePump();
        }
    }

    @Override
    public boolean onCheckIsTextEditor() { return textSession != 0; }

    @Override
    public InputConnection onCreateInputConnection(EditorInfo attrs) {
        if (textSession == 0) return null;
        attrs.inputType = InputType.TYPE_CLASS_TEXT;
        attrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_DONE;
        if (editor == null) editor = new EditorConnection();
        attrs.initialSelStart = Selection.getSelectionStart(editor.editable);
        attrs.initialSelEnd = Selection.getSelectionEnd(editor.editable);
        attrs.extras = new Bundle();
        attrs.extras.putLong("io.vui.text.session", textSession);
        attrs.extras.putLong("io.vui.text.revision", textRevision);
        return editor;
    }

    public void updateText(long session, long revision, String text, int anchor, int cursor) {
        InputMethodManager imm = (InputMethodManager)getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        boolean changed = textSession != session;
        textSession = session;
        textRevision = revision;
        if (session == 0) {
            editor = null;
            imm.hideSoftInputFromWindow(getWindowToken(), 0);
            imm.restartInput(this);
            return;
        }
        if (editor == null || changed) editor = new EditorConnection();
        applyingText = true;
        BaseInputConnection.removeComposingSpans(editor.editable);
        editor.editable.replace(0, editor.editable.length(), text);
        Selection.setSelection(editor.editable, Math.max(0, Math.min(anchor, text.length())),
            Math.max(0, Math.min(cursor, text.length())));
        applyingText = false;
        if (changed) imm.restartInput(this);
        imm.updateSelection(this, anchor, cursor, BaseInputConnection.getComposingSpanStart(editor.editable),
            BaseInputConnection.getComposingSpanEnd(editor.editable));
        if (changed) imm.showSoftInput(this, InputMethodManager.SHOW_IMPLICIT);
    }

    public void updateTextRevision(long revision) { textRevision = revision; }

    public String readClipboard() {
        ClipboardManager clipboard = (ClipboardManager)getContext().getSystemService(Context.CLIPBOARD_SERVICE);
        ClipData data = clipboard.getPrimaryClip();
        return data == null || data.getItemCount() == 0 ? "" : data.getItemAt(0).coerceToText(getContext()).toString();
    }

    public void writeClipboard(String text) {
        ClipboardManager clipboard = (ClipboardManager)getContext().getSystemService(Context.CLIPBOARD_SERVICE);
        clipboard.setPrimaryClip(ClipData.newPlainText("", text));
    }

    public void finishHost() {
        if (getContext() instanceof Activity) ((Activity)getContext()).finish();
    }

    private void setSystemBarIcons(boolean darkStatus, boolean darkNavigation) {
        if (getContext() instanceof Activity)
            VuiActivity.setSystemBarIcons(((Activity)getContext()).getWindow(), darkStatus, darkNavigation);
    }

    private final class EditorConnection extends BaseInputConnection {
        final Editable editable = new SpannableStringBuilder();
        final long session = textSession;
        int batch;

        EditorConnection() {
            super(VuiView.this, true);
            Selection.setSelection(editable, 0);
        }

        private boolean valid() { return editor == this && session == textSession && handle != 0; }
        @Override public Editable getEditable() { return valid() ? editable : null; }
        @Override public boolean beginBatchEdit() { if (!valid()) return false; ++batch; return true; }
        @Override public boolean endBatchEdit() { if (!valid()) return false; if (batch > 0 && --batch == 0) changed(); return batch > 0; }

        private void changed() {
            if (applyingText || batch != 0 || !valid()) return;
            nativeText(handle, session, textRevision, editable.toString(),
                Selection.getSelectionStart(editable), Selection.getSelectionEnd(editable),
                getComposingSpanStart(editable), getComposingSpanEnd(editable));
            pump();
        }

        @Override public boolean commitText(CharSequence text, int cursor) {
            if (!beginBatchEdit()) return false;
            boolean result = super.commitText(text, cursor); endBatchEdit(); return result;
        }
        @Override public boolean setComposingText(CharSequence text, int cursor) {
            if (!beginBatchEdit()) return false;
            boolean result = super.setComposingText(text, cursor); endBatchEdit(); return result;
        }
        @Override public boolean setComposingRegion(int start, int end) {
            if (!beginBatchEdit()) return false;
            boolean result = super.setComposingRegion(start, end); endBatchEdit(); return result;
        }
        @Override public boolean finishComposingText() {
            if (!beginBatchEdit()) return false;
            boolean result = super.finishComposingText(); endBatchEdit(); return result;
        }
        @Override public boolean setSelection(int start, int end) {
            if (!beginBatchEdit()) return false;
            boolean result = super.setSelection(start, end); endBatchEdit(); return result;
        }
        @Override public boolean deleteSurroundingText(int before, int after) {
            if (!beginBatchEdit()) return false;
            boolean result = super.deleteSurroundingText(before, after); endBatchEdit(); return result;
        }
        @Override public boolean deleteSurroundingTextInCodePoints(int before, int after) {
            if (!beginBatchEdit()) return false;
            boolean result = super.deleteSurroundingTextInCodePoints(before, after); endBatchEdit(); return result;
        }
        @Override public boolean sendKeyEvent(KeyEvent event) {
            if (!valid()) return false;
            return event.getAction() == KeyEvent.ACTION_DOWN ? onKeyDown(event.getKeyCode(), event) :
                onKeyUp(event.getKeyCode(), event);
        }
        @Override public boolean performEditorAction(int action) {
            if (!valid()) return false;
            onKeyDown(KeyEvent.KEYCODE_ENTER, new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_ENTER));
            onKeyUp(KeyEvent.KEYCODE_ENTER, new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_ENTER));
            return true;
        }
    }

    private static native long nativeCreate(VuiView view);
    private static native void nativeDestroy(long handle);
    private static native boolean nativeSurface(long handle, Surface surface);
    private static native void nativeResize(long handle, int width, int height);
    private static native int nativePump(long handle, long now, long sample, long expected);
    private static native void nativeActive(long handle, boolean active);
    private static native void nativeFocus(long handle, boolean focused);
    private static native void nativeWindowFocus(long handle, boolean focused);
    private static native boolean nativeBack(long handle);
    private static native boolean nativeCanBack(long handle);
    private static native void nativeRedraw(long handle);
    private static native void nativeRedrawAsync(long handle, Runnable done);
    private static native void nativeMetrics(long handle, float density, float fontScale, float refresh,
        int left, int top, int right, int bottom, int imeLeft, int imeTop, int imeRight, int imeBottom);
    private static native void nativePointer(long handle, int action, int pointer, int tool, int device,
        int buttons, int modifiers, long time, float x, float y, float pressure,
        long[] historyTimes, float[] history);
    private static native void nativeWheel(long handle, int device, float x, float y, float dx, float dy,
        long time, int modifiers);
    private static native boolean nativeKey(long handle, int code, boolean down, boolean repeat,
        int modifiers, int device, long time);
    private static native void nativeText(long handle, long session, long revision, String text,
        int anchor, int cursor, int composingStart, int composingEnd);
    private static native void nativeImeDismissed(long handle, long session);
}
