package com.example.sa2b;

import android.content.Intent;
import android.content.res.AssetManager;
import android.net.Uri;
import android.opengl.GLSurfaceView;
import android.os.Bundle;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.util.Log;
import android.widget.Button;
import android.widget.TextView;
import androidx.activity.result.ActivityResultLauncher;
import androidx.activity.result.contract.ActivityResultContracts;
import androidx.appcompat.app.AppCompatActivity;
import java.io.File;
import java.util.concurrent.Executors;
import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

public class MainActivity extends AppCompatActivity {
    static {
        // Carrega exatamente o nome da sua lib definida no CMakeLists.txt
        System.loadLibrary("sa2b_engine");
    }

    // Declarações JNI para o C++
    public static native void initAssetManager(AssetManager assetManager);
    public static native void nativeInit();
    public static native void nativeRender();
    public native boolean nativeExtractAll(int fileDescriptor, String outputDirPath);
    public native boolean nativeHasPendingTranslations(String basePath);
    public native void nativeStartTranslationAsync(String basePath);
    private GLSurfaceView glSurfaceView;
    private TextView txtProgress;
    private TextView txtCurrentFile;
    private File[] assetFiles;
    private int currentIndex = 0;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private Runnable assetCyclerRunnable;

    private final ActivityResultLauncher<Intent> folderPickerLauncher = registerForActivityResult(
        new ActivityResultContracts.StartActivityForResult(),
        result -> {
            if (result.getResultCode() == RESULT_OK && result.getData() != null) {
                Uri treeUri = result.getData().getData();
                if (treeUri != null) {
                    // Persiste a permissão para o app poder ler e escrever nessa pasta sempre que precisar
                    getContentResolver().takePersistableUriPermission(
                        treeUri,
                        Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_WRITE_URI_PERMISSION
                    );

                    // Aqui você pode salvar a Uri nas preferências ou iniciar a extração em massa
                    Log.i("SA2B_Modding", "Pasta selecionada com sucesso: " + treeUri);
                }
            }
        }
    );

    private final ActivityResultLauncher<String[]> isoPickerLauncher = registerForActivityResult(
        new ActivityResultContracts.OpenDocument(),
        uri -> {
            if (uri != null) {
                try {
                    // Obtém o FileDescriptor seguro da ISO selecionada
                    ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                    if (pfd != null) {
                        int fileDescriptor = pfd.getFd();

                        // Define o diretório Project-02 em Documents
                        File documentsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
                        File project02Dir = new File(documentsDir, "Project-02");
                        if (!project02Dir.exists()) {
                            project02Dir.mkdirs();
                        }

                        // Inicia a extração em background para não travar a tela
                        runExtraction(fileDescriptor, project02Dir.getAbsolutePath(), pfd);
                    }
                } catch (Exception e) {
                    Log.e("SA2B_ROM", "Erro ao abrir a ISO: " + e.getMessage());
                }
            }
        }
    );

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        // 1. Envia o gerenciador de assets do APK para o C++
        initAssetManager(getAssets());

        File documentsDir = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
        File project02Dir = new File(documentsDir, "Project-02");

        if (isFirstBoot(project02Dir)) {
            setContentView(R.layout.activity_main);

            // Vincula o botão do XML
            Button btnSelectIso = findViewById(R.id.btnSelectIso);
            btnSelectIso.setOnClickListener(v -> {
                // Abre o seletor de arquivos filtrando por qualquer tipo binário/genérico
                isoPickerLauncher.launch(new String[]{"*/*"});
            });
        } else if (nativeHasPendingTranslations(project02Dir.getAbsolutePath())) {
            setContentView(R.layout.activity_translation);
            nativeStartTranslationAsync(project02Dir.getAbsolutePath());
        } else {
            startGameEngine();
        }
    }

    // Método que o C++ vai chamar diretamente para atualizar a UI
    public void onExtractionProgress(int current, int total) {
        runOnUiThread(() -> {
            if (txtProgress != null) {
                int remaining = Math.max(0, total - current);
                txtProgress.setText("Extraindo: " + current + " de " + total
                        + " (Faltam: " + remaining + ")");
            }
        });
    }

    public void onTranslationPreparing(int prepared, int total) {
        runOnUiThread(() -> {
            TextView title = findViewById(R.id.txtTranslationTitle);
            TextView progress = findViewById(R.id.txtTranslationProgress);
            android.widget.ProgressBar bar = findViewById(R.id.translationProgressBar);
            if (title != null) title.setText("Preparando arquivos para tradução...");
            if (progress != null) {
                progress.setText(total > 0
                        ? "Arquivos preparados: " + prepared + " de " + total
                        : "Preparando arquivos...");
            }
            if (bar != null) {
                if (total > 0) {
                    bar.setIndeterminate(false);
                    bar.setMax(total);
                    bar.setProgress(Math.min(prepared, total));
                } else {
                    bar.setIndeterminate(true);
                }
            }
        });
    }

    public void onTranslationOrganizing() {
        runOnUiThread(() -> {
            TextView title = findViewById(R.id.txtTranslationTitle);
            TextView progress = findViewById(R.id.txtTranslationProgress);
            android.widget.ProgressBar bar = findViewById(R.id.translationProgressBar);
            if (title != null) title.setText("Organizando arquivos...");
            if (progress != null) progress.setText("Preparando texturas e modelos");
            if (bar != null) bar.setIndeterminate(true);
        });
    }

    public void onTranslationProgress(int translated, int total) {
        runOnUiThread(() -> {
            TextView title = findViewById(R.id.txtTranslationTitle);
            TextView progress = findViewById(R.id.txtTranslationProgress);
            android.widget.ProgressBar bar = findViewById(R.id.translationProgressBar);
            if (title != null) title.setText("Traduzindo assets do Sonic Adventure 2...");
            if (progress != null) {
                progress.setText("Traduzidos: " + translated + " de " + total
                        + " (Restam: " + Math.max(0, total - translated) + ")");
            }
            if (bar != null) {
                bar.setIndeterminate(false);
                bar.setMax(Math.max(1, total));
                bar.setProgress(Math.min(translated, Math.max(1, total)));
            }
        });
    }

    public void onTranslationFinished() {
        runOnUiThread(() -> {
            TextView progress = findViewById(R.id.txtTranslationProgress);
            if (progress != null) {
                progress.setText("Tradução concluída!");
            }
            startGameEngine();
        });
    }

    private boolean isFirstBoot(File projectDir) {
        if (!projectDir.exists()) return true;
        File[] files = projectDir.listFiles();
        return files == null || files.length == 0;
    }

    private void runExtraction(int fd, String outputDirPath, ParcelFileDescriptor pfd) {
        setContentView(R.layout.activity_splash);
        txtProgress = findViewById(R.id.txtProgress);

        Executors.newSingleThreadExecutor().execute(() -> {
            Log.i("SA2B_ROM", "Iniciando extração dos assets para a pasta Project-02...");

            boolean success = nativeExtractAll(fd, outputDirPath);

            try {
                pfd.close(); // Fecha o descritor após o uso
            } catch (Exception ignored) {}

            runOnUiThread(() -> {
                if (success) {
                    Log.i("SA2B_ROM", "Extração finalizada. Iniciando tradução...");

                    setContentView(R.layout.activity_translation);
                    nativeStartTranslationAsync(outputDirPath);
                } else {
                    Log.e("SA2B_ROM", "Falha na extração dos arquivos.");
                }
            });
        });
    }

    // GLSurfaceView personalizada para o motor SA2B
    public static class SA2BGLSurfaceView extends GLSurfaceView {
        public SA2BGLSurfaceView(android.content.Context context) {
            super(context);
            setEGLContextClientVersion(2);
            setRenderer(new SA2BRenderer());
        }
    }

    private void startGameEngine() {
        // Substitui a tela pela GLSurfaceView do motor OpenGL ES
        setContentView(R.layout.activity_game);
        glSurfaceView = findViewById(R.id.glSurfaceView);
        initializeAssetCycle();
    }

    private void initializeAssetCycle() {
        // O layout activity_game já foi carregado antes deste método ser chamado.
        txtCurrentFile = findViewById(R.id.txtCurrentFile);

        try {
            File documentsDir = Environment.getExternalStoragePublicDirectory(
                    Environment.DIRECTORY_DOCUMENTS);
            File modelsDir = new File(documentsDir, "Project-02/models");

            if (modelsDir.exists()) {
                assetFiles = modelsDir.listFiles();
            }

            if (assetFiles != null && assetFiles.length > 0) {
                startAssetCycle();
            } else if (txtCurrentFile != null) {
                txtCurrentFile.setText("Pasta encontrada, mas vazia ou sem acesso.");
            }
        } catch (Exception e) {
            Log.e("SA2B_DEBUG", "Erro ao carregar arquivos: " + e.getMessage(), e);
            if (txtCurrentFile != null) {
                txtCurrentFile.setText("Não foi possível acessar a pasta de assets.");
            }
        }
    }

    private void startAssetCycle() {
        assetCyclerRunnable = new Runnable() {
            @Override
            public void run() {
                if (assetFiles != null && assetFiles.length > 0) {
                    File currentFile = assetFiles[currentIndex];
                    txtCurrentFile.setText("Exibindo: " + currentFile.getName() + " ("
                            + (currentIndex + 1) + "/" + assetFiles.length + ")");
                    currentIndex = (currentIndex + 1) % assetFiles.length;
                }
                handler.postDelayed(this, 5000);
            }
        };
        handler.post(assetCyclerRunnable);
    }

    // Renderizador personalizado conectado ao pipeline C++
    public static class SA2BRenderer implements GLSurfaceView.Renderer {
        @Override
        public void onSurfaceCreated(GL10 gl, EGLConfig config) {
            nativeInit();
        }

        @Override
        public void onSurfaceChanged(GL10 gl, int width, int height) {
            // Ajustes de viewport quando a superfície mudar
        }

        @Override
        public void onDrawFrame(GL10 gl) {
            nativeRender();
        }
    }

    // A seleção de ISO fica disponível somente no botão da tela inicial.
    // Não há mais um listener global de toque nesta Activity.
    public void openFolderSelector() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        folderPickerLauncher.launch(intent);
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (glSurfaceView != null) glSurfaceView.onResume();
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (glSurfaceView != null) glSurfaceView.onPause();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        if (assetCyclerRunnable != null) {
            handler.removeCallbacks(assetCyclerRunnable);
        }
    }
}