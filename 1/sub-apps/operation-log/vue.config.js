const { name } = require('./package');

module.exports = {
  transpileDependencies: true,
  publicPath: '//localhost:8003',
  devServer: {
    port: 8003,
    headers: {
      'Access-Control-Allow-Origin': '*'
    }
  },
  configureWebpack: {
    output: {
      library: `${name}-[name]`,
      libraryTarget: 'umd',
      jsonpFunction: `webpackJsonp_${name}`
    }
  }
};
