package com.example.sa2b;

import android.content.Context;
import android.opengl.GLSurfaceView;
import android.util.AttributeSet;

public class SA2BGLSurfaceView extends GLSurfaceView {

    private final MainActivity.SA2BRenderer renderer;

    // Construtor usado quando a view é criada via código Java.
    public SA2BGLSurfaceView(Context context) {
        super(context);
        renderer = init();
    }

    // Construtor obrigatório quando a view é criada a partir de XML.
    public SA2BGLSurfaceView(Context context, AttributeSet attrs) {
        super(context, attrs);
        renderer = init();
    }

    private MainActivity.SA2BRenderer init() {
        // Define a versão do OpenGL ES.
        setEGLContextClientVersion(2);

        // Mantém o renderer nativo já usado pelo projeto.
        MainActivity.SA2BRenderer newRenderer = new MainActivity.SA2BRenderer();
        setRenderer(newRenderer);

        // Renderização contínua para o motor do jogo.
        setRenderMode(GLSurfaceView.RENDERMODE_CONTINUOUSLY);
        return newRenderer;
    }
}
