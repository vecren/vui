package io.vui;

import android.app.Activity;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Build;
import android.view.Window;
import android.view.View;
import android.view.WindowInsetsController;
import android.view.WindowManager;
import android.window.OnBackInvokedCallback;
import android.window.OnBackInvokedDispatcher;

public class VuiActivity extends Activity {
    private VuiView view;
    private OnBackInvokedCallback backCallback;

    @Override
    public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().requestFeature(Window.FEATURE_NO_TITLE);
        enableEdgeToEdge();
        view = new VuiView(this);
        setContentView(view);
    }

    @SuppressWarnings("deprecation")
    private void enableEdgeToEdge() {
        Window window = getWindow();
        View decor = window.getDecorView();
        window.addFlags(WindowManager.LayoutParams.FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
        window.clearFlags(WindowManager.LayoutParams.FLAG_TRANSLUCENT_STATUS |
            WindowManager.LayoutParams.FLAG_TRANSLUCENT_NAVIGATION);
        window.setStatusBarColor(Color.TRANSPARENT);
        window.setNavigationBarColor(Color.TRANSPARENT);
        window.setNavigationBarDividerColor(Color.TRANSPARENT);
        window.setStatusBarContrastEnforced(false);
        window.setNavigationBarContrastEnforced(false);
        WindowManager.LayoutParams attributes = window.getAttributes();
        if (Build.VERSION.SDK_INT >= 30) {
            window.setDecorFitsSystemWindows(false);
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_ALWAYS;
        } else {
            decor.setSystemUiVisibility(decor.getSystemUiVisibility() | View.SYSTEM_UI_FLAG_LAYOUT_STABLE |
                View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION);
            attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
        }
        window.setAttributes(attributes);
    }

    @SuppressWarnings("deprecation")
    static void setSystemBarIcons(Window window, boolean darkStatus, boolean darkNavigation) {
        if (Build.VERSION.SDK_INT >= 30) {
            WindowInsetsController controller = window.getInsetsController();
            if (controller == null) return;
            int status = WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS;
            int navigation = WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            controller.setSystemBarsAppearance((darkStatus ? status : 0) | (darkNavigation ? navigation : 0),
                status | navigation);
        } else {
            View decor = window.getDecorView();
            int status = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
            int navigation = View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
            int flags = decor.getSystemUiVisibility() & ~(status | navigation);
            decor.setSystemUiVisibility(flags | (darkStatus ? status : 0) | (darkNavigation ? navigation : 0));
        }
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (view != null) view.setActive(true);
    }

    @Override
    protected void onPause() {
        if (view != null) view.setActive(false);
        super.onPause();
    }

    @Override
    protected void onDestroy() {
        setBackEnabled(false);
        if (view != null) {
            view.close();
            view = null;
        }
        super.onDestroy();
    }

    @Override
    public void onBackPressed() {
        if (view == null || !view.back()) super.onBackPressed();
    }

    void setBackEnabled(boolean enabled) {
        if (Build.VERSION.SDK_INT < 33) return;
        if (enabled && backCallback == null) {
            backCallback = () -> { if (view == null || !view.back()) finish(); };
            getOnBackInvokedDispatcher().registerOnBackInvokedCallback(OnBackInvokedDispatcher.PRIORITY_DEFAULT, backCallback);
        } else if (!enabled && backCallback != null) {
            getOnBackInvokedDispatcher().unregisterOnBackInvokedCallback(backCallback);
            backCallback = null;
        }
    }
}
