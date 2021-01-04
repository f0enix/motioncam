package com.motioncam;

import android.os.Bundle;

import androidx.appcompat.app.AppCompatActivity;

import com.motioncam.ui.SettingsFragment;

public class SettingsActivity extends AppCompatActivity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.settings_activity);

        if (savedInstanceState == null) {
            SettingsFragment fragment = SettingsFragment.newInstance();

            getSupportFragmentManager()
                    .beginTransaction()
                    .replace(R.id.container, fragment)
                    .commitNow();
        }
    }

}
